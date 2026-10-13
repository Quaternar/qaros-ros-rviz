// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/rviz_display_context_adapter.hpp"

#include <sstream>

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>

#include "rviz_common/display_context.hpp"
#include "rviz_common/view_manager.hpp"
#include "rviz_common/view_controller.hpp"

namespace qar_rviz_streaming
{

RVizDisplayContextAdapter::RVizDisplayContextAdapter(rviz_common::DisplayContext * context)
: context_(context)
{
}

bool RVizDisplayContextAdapter::isValid() const
{
  return context_ != nullptr && getSceneManager() != nullptr;
}

Ogre::SceneManager * RVizDisplayContextAdapter::getSceneManager() const
{
  return context_ != nullptr ? context_->getSceneManager() : nullptr;
}

Ogre::Camera * RVizDisplayContextAdapter::getViewCamera() const
{
  if (context_ == nullptr) {
    return nullptr;
  }
  rviz_common::ViewManager * vm = context_->getViewManager();
  if (vm == nullptr) {
    return nullptr;
  }
  rviz_common::ViewController * vc = vm->getCurrent();
  if (vc == nullptr) {
    return nullptr;
  }
  return vc->getCamera();
}

void RVizDisplayContextAdapter::lockRender()
{
  if (context_ != nullptr) {
    context_->lockRender();
  }
}

void RVizDisplayContextAdapter::unlockRender()
{
  if (context_ != nullptr) {
    context_->unlockRender();
  }
}

void RVizDisplayContextAdapter::queueRender()
{
  if (context_ != nullptr) {
    context_->queueRender();
  }
}

std::string RVizDisplayContextAdapter::sceneManagerName() const
{
  Ogre::SceneManager * sm = getSceneManager();
  if (sm == nullptr) {
    return "<no scene manager>";
  }
  return std::string(sm->getName().c_str());
}

std::size_t RVizDisplayContextAdapter::cameraCount() const
{
  Ogre::SceneManager * sm = getSceneManager();
  if (sm == nullptr) {
    return 0;
  }
  return sm->getCameras().size();
}

std::size_t RVizDisplayContextAdapter::sceneNodeChildCount() const
{
  Ogre::SceneManager * sm = getSceneManager();
  if (sm == nullptr) {
    return 0;
  }
  Ogre::SceneNode * root = sm->getRootSceneNode();
  return root != nullptr ? static_cast<std::size_t>(root->numChildren()) : 0;
}

std::uint64_t RVizDisplayContextAdapter::frameCount() const
{
  return context_ != nullptr ? context_->getFrameCount() : 0;
}

std::string RVizDisplayContextAdapter::statusSummary() const
{
  std::ostringstream os;
  os << "DisplayContext: " << (context_ != nullptr ? "valid" : "NULL") << "\n";
  os << "SceneManager:   " << (getSceneManager() != nullptr ? "valid" : "NULL")
     << " (" << sceneManagerName() << ")\n";
  os << "Cameras:        " << cameraCount() << "\n";
  os << "Root children:  " << sceneNodeChildCount() << "\n";
  os << "Frame count:    " << frameCount();
  return os.str();
}

}  // namespace qar_rviz_streaming

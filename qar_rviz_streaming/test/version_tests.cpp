// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The version of the build (cmake/QarRvizVersion.cmake): X.Y.Z from the file VERSION, with the part
// that git or the release build adds, and the same X.Y.Z in package.xml for the ROS tools.

#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>

namespace
{

std::string read_file(const char * path)
{
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

TEST(PluginVersion, IsTheVersionFileWithTheDerivedPart)
{
  const std::string version = QAR_RVIZ_PLUGIN_VERSION;
  std::smatch match;
  std::string file = read_file(QAR_TEST_VERSION_FILE);
  ASSERT_TRUE(std::regex_search(file, match, std::regex(R"(^\s*([0-9]+\.[0-9]+\.[0-9]+)\s*$)")))
    << QAR_TEST_VERSION_FILE;
  const std::string core = match[1];
  std::cout << "qar_rviz_plugin " << version << " (VERSION " << core << ")\n";

  EXPECT_EQ(version.rfind(core, 0), 0u) << version;
  // X.Y.Z, X.Y.Z-rc.N, X.Y.Z-dev[.N[+sha]], X.Y.Z-pr.N or a release build's own suffix.
  EXPECT_TRUE(std::regex_match(version, std::regex(R"([0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.+-]+)?)")))
    << version;

  const std::string xml = read_file(QAR_TEST_PACKAGE_XML);
  ASSERT_TRUE(std::regex_search(xml, match, std::regex(R"(<version>\s*([^<\s]+)\s*</version>)")))
    << QAR_TEST_PACKAGE_XML;
  EXPECT_EQ(match[1].str(), core);
}

}  // namespace

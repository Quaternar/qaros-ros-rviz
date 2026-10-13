// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The page-side runtime (qar-ui-runtime/1), embedded verbatim by
// render_html_page. It implements the page half of docs/WIRE_PROTOCOL.md
// and mirrors three pieces of C++ exactly: FieldPath's grammar, set_path's
// create-on-the-way semantics, and the all-or-nothing patch rule.
//
// Rules the script keeps:
//   * Values reach the DOM through textContent / value / attributes only,
//     never innerHTML, so data cannot become markup.
//   * Image URLs are allow-listed (http, https, data:image/, blob); anything
//     else, javascript: above all, is dropped.
//   * Nothing depends on hover; the pointer may be a jittering hand ray.
//   * ES5 only (tests ban =>, template literals, let/const, Number.isInteger),
//     no modules, no external references: the page must work offline. Target:
//     Ultralight 1.3 (WebKit 610.4.3, JavaScriptCore at Safari 14 level) -
//     see docs/SDK_COMPATIBILITY.md ("The QarOS in-AR browser").
//   * The page says why it is dead. A capability probe at boot writes the
//     missing feature into #qar-diag rather than leaving a blank rectangle.
//
// Split into chunks under MSVC's 16 KB single-literal limit and joined at run
// time. The text must never contain "</script" (it sits inside one).

#include <string>

#include "qar_ui_model/html_renderer.hpp"

namespace qar_ui_model
{

namespace
{

const char kRuntimeChunk1[] = R"QARJS(/* qar-ui-runtime/1 - embedded by qar_ui_model. Contract: docs/WIRE_PROTOCOL.md */
(function () {
  'use strict';
  var PROTOCOL_VERSION = 1;
  var RUNTIME = 'qar-ui-runtime/1';
  var EM_DASH = '\u2014';

  function readJson(id, fallback) {
    var el = document.getElementById(id);
    if (!el) { return fallback; }
    try { return JSON.parse(el.textContent); } catch (e) { return fallback; }
  }
  function isObject(v) { return v !== null && typeof v === 'object' && !Array.isArray(v); }
  function num(v) { return typeof v === 'number' && isFinite(v) ? v : 0; }
  function isInt(v) { return typeof v === 'number' && isFinite(v) && Math.floor(v) === v; }
  /* Ultralight synthesises key events from the host; keyCode is the one field
     every engine fills in, so Enter is checked both ways. */
  function isEnter(ev) { return !!ev && (ev.key === 'Enter' || ev.keyCode === 13); }

  var config = readJson('qar-config', {});
  if (!isObject(config)) { config = {}; }
  var model = readJson('qar-model', null);
  if (!isObject(model) || !isObject(model.root)) {
    model = { version: 1, root: { id: 'root', type: 'panel', children: [] } };
  }
  var doc = readJson('qar-data', {});
  if (!isObject(doc)) { doc = {}; }

  /* ---- paths: the FieldPath grammar, segment ('.' segment)*, segment = ident ('[' digits ']')? ---- */
  var IDENT = /^[A-Za-z_][A-Za-z0-9_]*$/;
  function parsePath(text) {
    if (typeof text !== 'string') { return null; }
    if (text === '') { return []; }
    var out = [];
    var parts = text.split('.');
    for (var i = 0; i < parts.length; i++) {
      var part = parts[i];
      var name = part;
      var index = null;
      var open = part.indexOf('[');
      if (open >= 0) {
        if (part.charAt(part.length - 1) !== ']') { return null; }
        name = part.substring(0, open);
        var digits = part.substring(open + 1, part.length - 1);
        if (!/^[0-9]+$/.test(digits)) { return null; }
        index = parseInt(digits, 10);
      }
      if (!IDENT.test(name)) { return null; }
      out.push({ name: name, index: index });
    }
    return out;
  }
  function getPath(root, segs) {
    var cur = root;
    for (var i = 0; i < segs.length; i++) {
      if (!isObject(cur)) { return undefined; }
      cur = cur[segs[i].name];
      if (cur === undefined) { return undefined; }
      if (segs[i].index !== null) {
        if (!Array.isArray(cur) || segs[i].index >= cur.length) { return undefined; }
        cur = cur[segs[i].index];
      }
    }
    return cur;
  }
  /* Same rules as qar_ui_model::set_path: create objects/arrays on the way, refuse a kind clash. */
  function setPath(root, segs, value) {
    var cur = root;
    for (var i = 0; i < segs.length; i++) {
      var seg = segs[i];
      var last = i === segs.length - 1;
      if (!isObject(cur)) { return false; }
      var next = cur[seg.name];
      if (seg.index !== null) {
        if (next === undefined || next === null) { next = []; cur[seg.name] = next; }
        if (!Array.isArray(next)) { return false; }
        while (next.length <= seg.index) { next.push(null); }
        if (last) { next[seg.index] = value; return true; }
        if (next[seg.index] === null || next[seg.index] === undefined) { next[seg.index] = {}; }
        cur = next[seg.index];
      } else {
        if (last) { cur[seg.name] = value; return true; }
        if (next === undefined || next === null) { next = {}; cur[seg.name] = next; }
        cur = next;
      }
    }
    return true;
  }
  function clone(v) { return JSON.parse(JSON.stringify(v)); }

  /* ---- formatting ---- */
  function formatNumber(v, w) {
    if (typeof v !== 'number' || !isFinite(v)) { return v === null || v === undefined ? EM_DASH : String(v); }
    if (w && w.integer) { return String(Math.round(v)); }
    var p = (w && typeof w.precision === 'number') ? w.precision : 3;
    if (p < 0) { p = 0; }
    if (p > 17) { p = 17; }
    return v.toFixed(p);
  }
  function formatValue(v, w) {
    if (v === undefined || v === null) { return EM_DASH; }
    if (typeof v === 'number') { return formatNumber(v, w); }
    if (typeof v === 'boolean') { return v ? 'true' : 'false'; }
    if (typeof v === 'string') { return v; }
    try { return JSON.stringify(v); } catch (e) { return String(v); }
  }
  function sameValue(a, b) {
    if (typeof a === 'number' && typeof b === 'number') { return a === b; }
    return a === b;
  }
  function decimalsOf(step) {
    var s = String(step);
    var dot = s.indexOf('.');
    var e = s.indexOf('e-');
    if (e >= 0) { return parseInt(s.substring(e + 2), 10) + (dot >= 0 ? e - dot - 1 : 0); }
    return dot >= 0 ? s.length - dot - 1 : 0;
  }
  function roundTo(v, step) {
    if (!(step > 0)) { return v; }
    var r = Math.round(v / step) * step;
    return parseFloat(r.toFixed(Math.min(17, decimalsOf(step))));
  }
  function clampTo(v, w) {
    if (typeof w.min === 'number' && v < w.min) { v = w.min; }
    if (typeof w.max === 'number' && v > w.max) { v = w.max; }
    return v;
  }
  function stepOf(w) {
    if (typeof w.step === 'number' && w.step > 0) { return w.step; }
    if (w.integer) { return 1; }
    if (typeof w.min === 'number' && typeof w.max === 'number' && w.max > w.min) { return (w.max - w.min) / 100; }
    return 0.1;
  }

  /* ---- quaternion <-> roll/pitch/yaw (degrees, ROS ZYX convention) ---- */
  function quatToEuler(q) {
    var x = num(q.x), y = num(q.y), z = num(q.z), w = num(q.w);
    var n = Math.sqrt(x * x + y * y + z * z + w * w);
    if (n > 0) { x /= n; y /= n; z /= n; w /= n; }
    var sinp = 2 * (w * y - z * x);
    if (sinp > 1) { sinp = 1; }
    if (sinp < -1) { sinp = -1; }
    var d = 180 / Math.PI;
    return {
      roll: Math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * d,
      pitch: Math.asin(sinp) * d,
      yaw: Math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) * d
    };
  }
  function eulerToQuat(e) {
    var r = num(e.roll) * Math.PI / 360, p = num(e.pitch) * Math.PI / 360, y = num(e.yaw) * Math.PI / 360;
    var cr = Math.cos(r), sr = Math.sin(r), cp = Math.cos(p), sp = Math.sin(p), cy = Math.cos(y), sy = Math.sin(y);
    return {
      x: sr * cp * cy - cr * sp * sy,
      y: cr * sp * cy + sr * cp * sy,
      z: cr * cp * sy - sr * sp * cy,
      w: cr * cp * cy + sr * sp * sy
    };
  }
)QARJS";

const char kRuntimeChunk2[] = R"QARJS(
  /* ---- DOM helpers: values go in through textContent/value/attributes only ---- */
  function q(el, role, component) {
    if (!el) { return null; }
    var sel = '[data-qar-role="' + role + '"]';
    if (component) { sel += '[data-qar-component="' + component + '"]'; }
    return el.querySelector(sel);
  }
  function qa(el, role) { return el ? el.querySelectorAll('[data-qar-role="' + role + '"]') : []; }
  function setText(el, text) { if (el && el.textContent !== text) { el.textContent = text; } }
  function setAttr(el, name, value) { if (el && el.getAttribute(name) !== value) { el.setAttribute(name, value); } }

  /* ---- registry ---- */
  var entries = [];
  var byId = {};
  function register(w) {
    if (!isObject(w) || typeof w.id !== 'string') { return; }
    var el = document.querySelector('[data-qar-id="' + w.id + '"]');
    var entry = { w: w, el: el, segs: parsePath(w.bind || '') || [], page: 0, samples: [], dirty: false, dragging: false, armed: null };
    byId[w.id] = entry;
    entries.push(entry);
    if (el) { wire(entry); }
    var children = Array.isArray(w.children) ? w.children : [];
    for (var i = 0; i < children.length; i++) { register(children[i]); }
  }

  /* ---- connection state ---- */
  var ws = null;
  var state = 'offline';
  var outSeq = 0;
  var lastFrameAt = 0;
  var lastSentAt = 0;
  var listeners = [];
  var pending = {};
  function setState(s) {
    state = s;
    setAttr(document.getElementById('qar-connection'), 'data-qar-state', s);
    setText(document.getElementById('qar-connection-text'), s === 'online' ? 'Live' : (s === 'stale' ? 'Stale' : 'Offline'));
  }
  function setStatusLine(level, text) {
    var el = document.getElementById('qar-status');
    setAttr(el, 'data-qar-level', level || 'neutral');
    setText(el, text || '');
  }

  /* ---- per-type refresh ---- */
  function refreshAll() { for (var i = 0; i < entries.length; i++) { refresh(entries[i]); } }
  function refresh(entry) {
    var w = entry.w, el = entry.el;
    if (!el) { return; }
    var v = getPath(doc, entry.segs);
    var input;
    switch (w.type) {
      case 'value':
        setText(q(el, 'value'), formatValue(v, w));
        break;
      case 'number':
        input = q(el, 'input');
        if (input && document.activeElement !== input) { input.value = typeof v === 'number' ? formatNumber(v, w) : ''; }
        break;
      case 'text':
        input = q(el, 'input');
        if (input && document.activeElement !== input && !entry.dirty) { input.value = typeof v === 'string' ? v : (v === null || v === undefined ? '' : formatValue(v, w)); }
        break;
      case 'toggle':
        setAttr(q(el, 'toggle'), 'aria-checked', v === true ? 'true' : 'false');
        setText(q(el, 'text'), v === null || v === undefined ? EM_DASH : (v === true ? 'On' : 'Off'));
        break;
      case 'slider':
        input = q(el, 'input');
        if (input && !entry.dragging && typeof v === 'number') { input.value = String(v); }
        setText(q(el, 'value'), formatValue(v, w));
        break;
      case 'choice':
        var opts = qa(el, 'option');
        for (var i = 0; i < opts.length; i++) {
          var match = Array.isArray(w.choices) && w.choices[i] && sameValue(w.choices[i].value, v);
          setAttr(opts[i], 'aria-checked', match ? 'true' : 'false');
        }
        break;
      case 'vector3':
        refreshComponents(entry, isObject(v) ? v : {}, componentNames(w), w);
        break;
      case 'quaternion':
        var qv = isObject(v) ? v : {};
        refreshComponents(entry, qv, ['x', 'y', 'z', 'w'], { precision: typeof w.precision === 'number' ? w.precision : 4 });
        refreshComponents(entry, isObject(v) ? quatToEuler(qv) : {}, ['roll', 'pitch', 'yaw'], { precision: 1 });
        break;
      case 'table':
        refreshTable(entry, v);
        break;
      case 'gauge':
        refreshGauge(entry, v);
        break;
      case 'status':
        refreshStatus(entry, v);
        break;
      case 'chart':
        if (typeof v === 'number' && isFinite(v)) { entry.samples.push({ t: Date.now(), v: v }); }
        setText(q(el, 'value'), formatValue(v, w));
        drawChart(entry);
        break;
      case 'image':
        refreshImage(entry, v);
        break;
      case 'json':
        setText(q(el, 'json'), v === undefined ? EM_DASH : JSON.stringify(v, null, 2));
        break;
      default:
        break;
    }
  }
  function componentNames(w) {
    return Array.isArray(w.components) && w.components.length === 3 ? w.components : ['x', 'y', 'z'];
  }
  function refreshComponents(entry, obj, names, fmt) {
    for (var i = 0; i < names.length; i++) {
      var v = obj[names[i]];
      var input = q(entry.el, 'input', names[i]);
      if (input) {
        if (document.activeElement !== input) { input.value = typeof v === 'number' ? formatNumber(v, fmt) : ''; }
      } else {
        setText(q(entry.el, 'value', names[i]), typeof v === 'number' ? formatNumber(v, fmt) : formatValue(v, fmt));
      }
    }
  }
  function matchRule(rules, v) {
    if (!Array.isArray(rules)) { return null; }
    for (var i = 0; i < rules.length; i++) {
      var r = rules[i];
      if (!isObject(r)) { continue; }
      if (r.equals !== undefined && r.equals !== null) {
        if (sameValue(r.equals, v)) { return r; }
        continue;
      }
      if (typeof v !== 'number') { continue; }
      if ((typeof r.min !== 'number' || v >= r.min) && (typeof r.max !== 'number' || v <= r.max)) { return r; }
    }
    return null;
  }
  function refreshGauge(entry, v) {
    var w = entry.w, el = entry.el;
    var lo = num(w.min), hi = typeof w.max === 'number' ? w.max : 1;
    var fill = q(el, 'fill');
    var pct = 0;
    if (typeof v === 'number' && isFinite(v) && hi > lo) {
      pct = (v - lo) / (hi - lo);
      if (pct < 0) { pct = 0; }
      if (pct > 1) { pct = 1; }
    }
    if (fill) { fill.style.width = (pct * 100).toFixed(1) + '%'; }
    var rule = matchRule(w.rules, v);
    setAttr(fill, 'data-qar-level', rule ? (rule.level || 'neutral') : 'neutral');
    setText(q(el, 'value'), formatValue(v, w));
  }
  function refreshStatus(entry, v) {
    var w = entry.w, el = entry.el;
    var rule = matchRule(w.rules, v);
    var level, text;
    if (rule) {
      level = rule.level || 'neutral';
      text = rule.text || formatValue(v, w);
    } else if (v === true) { level = 'ok'; text = 'OK'; }
    else if (v === false) { level = 'neutral'; text = 'Off'; }
    else if (v === null || v === undefined) { level = 'stale'; text = EM_DASH; }
    else { level = 'neutral'; text = formatValue(v, w); }
    setAttr(q(el, 'status'), 'data-qar-level', level);
    setText(q(el, 'text'), text);
  }
  function refreshImage(entry, v) {
    var img = q(entry.el, 'img');
    if (!img) { return; }
    var url = imageUrl(v);
    if (url === null) {
      img.removeAttribute('src');
      img.hidden = true;
    } else {
      if (img.getAttribute('src') !== url) { img.setAttribute('src', url); }
      img.hidden = false;
    }
  }
  /* Only URLs a picture can come from. A string field is untrusted input. */
  function imageUrl(v) {
    if (typeof v !== 'string') { return null; }
    var s = v.trim();
    if (s === '' || s.length > 4 * 1024 * 1024) { return null; }
    var m = /^([a-z][a-z0-9+.-]*):/i.exec(s);
    if (!m) { return s; }
    var scheme = m[1].toLowerCase();
    if (scheme === 'http' || scheme === 'https' || scheme === 'blob') { return s; }
    if (scheme === 'data' && /^data:image\//i.test(s)) { return s; }
    return null;
  }
)QARJS";

const char kRuntimeChunk3[] = R"QARJS(
  function refreshTable(entry, v) {
    var w = entry.w, el = entry.el;
    var rows = Array.isArray(v) ? v : [];
    var pageSize = (w.max_rows > 0 ? w.max_rows : (config.table_page_rows > 0 ? config.table_page_rows : 8));
    var pages = Math.max(1, Math.ceil(rows.length / pageSize));
    if (entry.page >= pages) { entry.page = pages - 1; }
    if (entry.page < 0) { entry.page = 0; }
    var body = q(el, 'rows');
    if (!body) { return; }
    while (body.firstChild) { body.removeChild(body.firstChild); }
    var columns = Array.isArray(w.columns) ? w.columns : [];
    var start = entry.page * pageSize;
    var end = Math.min(rows.length, start + pageSize);
    for (var r = start; r < end; r++) {
      var tr = document.createElement('tr');
      for (var c = 0; c < columns.length; c++) {
        var td = document.createElement('td');
        var segs = parsePath(columns[c].path || '') || [];
        var cell = segs.length ? getPath(rows[r], segs) : rows[r];
        if (typeof cell === 'number') { td.className = 'qar-num'; }
        td.textContent = formatValue(cell, { precision: typeof columns[c].precision === 'number' ? columns[c].precision : (isInt(cell) ? 0 : 3) });
        tr.appendChild(td);
      }
      body.appendChild(tr);
    }
    var info = q(el, 'pageinfo');
    setText(info, rows.length === 0 ? 'No rows' : (start + 1) + '\u2013' + end + ' of ' + rows.length);
    var prev = q(el, 'prev'), next = q(el, 'next');
    if (prev) { prev.disabled = entry.page === 0; }
    if (next) { next.disabled = entry.page >= pages - 1; }
    var pager = prev ? prev.parentNode : null;
    if (pager) { pager.hidden = rows.length <= pageSize; }
  }

  function drawChart(entry) {
    var w = entry.w, el = entry.el;
    var line = q(el, 'line');
    if (!line) { return; }
    var now = Date.now();
    var windowMs = (w.window_seconds > 0 ? w.window_seconds : 30) * 1000;
    var maxPoints = w.max_points >= 2 ? w.max_points : 600;
    var s = entry.samples;
    while (s.length && s[0].t < now - windowMs) { s.shift(); }
    while (s.length > maxPoints) { s.shift(); }
    if (!s.length) { line.setAttribute('points', ''); return; }
    var lo = w.min, hi = w.max;
    if (typeof lo !== 'number' || typeof hi !== 'number' || !(hi > lo)) {
      lo = Infinity; hi = -Infinity;
      for (var i = 0; i < s.length; i++) { if (s[i].v < lo) { lo = s[i].v; } if (s[i].v > hi) { hi = s[i].v; } }
      if (lo === hi) { lo -= 1; hi += 1; } else { var pad = (hi - lo) * 0.1; lo -= pad; hi += pad; }
    }
    var pts = [];
    for (var j = 0; j < s.length; j++) {
      var x = ((s[j].t - (now - windowMs)) / windowMs) * 600;
      var y = 220 - ((s[j].v - lo) / (hi - lo)) * 220;
      if (y < 0) { y = 0; }
      if (y > 220) { y = 220; }
      pts.push(x.toFixed(1) + ',' + y.toFixed(1));
    }
    line.setAttribute('points', pts.join(' '));
    setText(q(el, 'ymin'), formatNumber(lo, w));
    setText(q(el, 'ymax'), formatNumber(hi, w));
  }

  /* ---- outbound ---- */
  function send(msg) {
    if (ws && ws.readyState === 1) {
      try { ws.send(JSON.stringify(msg)); lastSentAt = Date.now(); return true; } catch (e) { return false; }
    }
    return false;
  }
  function emit(controlId, action, path, value) {
    var msg = { v: PROTOCOL_VERSION, type: 'control', seq: ++outSeq, ts: Date.now() / 1000,
      control: String(controlId), action: action || '', path: path || '', value: value === undefined ? null : value };
    send(msg);
    for (var i = 0; i < listeners.length; i++) { try { listeners[i](msg); } catch (e) { /* listener's problem */ } }
    return msg;
  }
  function emitFor(entry, value) {
    var msg = emit(entry.w.id, entry.w.action || '', entry.w.bind || '', value);
    pending[msg.seq] = entry;
    return msg;
  }
  /* Optimistic: the page shows the new value at once; the server's data frame confirms or corrects it. */
  function commit(entry, value) {
    if (entry.segs.length) { setPath(doc, entry.segs, value); }
    refresh(entry);
    return emitFor(entry, value);
  }
  function showAck(entry, msg) {
    var ack = q(entry.el, 'ack');
    var level = msg.ok ? 'ok' : 'error';
    if (ack) {
      setAttr(ack, 'data-qar-level', level);
      setText(ack, msg.text || (msg.ok ? 'OK' : 'Failed'));
      setTimeout(function () { setText(ack, ''); }, 4000);
    }
    var button = q(entry.el, 'button');
    if (button) {
      setAttr(button, 'data-qar-ack', level);
      setTimeout(function () { button.removeAttribute('data-qar-ack'); }, 1500);
    }
    if (!msg.ok && !ack) { setStatusLine('error', msg.text || ('Control ' + entry.w.id + ' failed')); }
  }

  /* ---- wiring ---- */
  function on(el, type, fn) { if (el) { el.addEventListener(type, fn); } }
  /* The +/- buttons: the primary way to change a number in mid-air. */
  function wireStepButtons(entry, spec, component, getter, setter) {
    function bump(sign) {
      var step = stepOf(spec);
      var base = getter();
      if (typeof base !== 'number' || !isFinite(base)) { base = typeof spec.min === 'number' ? spec.min : 0; }
      setter(clampTo(roundTo(base + sign * step, step), spec));
    }
    on(q(entry.el, 'dec', component), 'click', function () { bump(-1); });
    on(q(entry.el, 'inc', component), 'click', function () { bump(1); });
  }
  /* The typed field next to them, committed on change (blur / Enter). */
  function wireStepInput(entry, spec, component, setter) {
    var input = q(entry.el, 'input', component);
    on(input, 'change', function () {
      var v = Number(input.value);
      if (input.value !== '' && isFinite(v)) {
        if (spec.integer) { v = Math.round(v); }
        setter(clampTo(v, spec));
      } else {
        refresh(entry);
      }
    });
    on(input, 'keydown', function (ev) { if (isEnter(ev)) { input.blur(); } });
  }
  function wireStepper(entry, spec, component, getter, setter) {
    wireStepButtons(entry, spec, component, getter, setter);
    wireStepInput(entry, spec, component, setter);
  }
  function wire(entry) {
    var w = entry.w, el = entry.el;
    var editable = !!w.editable;
    switch (w.type) {
      case 'button':
        wireButton(entry);
        break;
      case 'toggle':
        if (editable) { on(q(el, 'toggle'), 'click', function () { commit(entry, getPath(doc, entry.segs) !== true); }); }
        break;
      case 'number':
        if (editable) {
          wireStepper(entry, w, null, function () { return getPath(doc, entry.segs); }, function (v) { commit(entry, v); });
        }
        break;
      case 'slider':
        if (editable) {
          var range = q(el, 'input');
          on(range, 'input', function () { entry.dragging = true; setText(q(el, 'value'), formatValue(Number(range.value), w)); });
          on(range, 'change', function () { entry.dragging = false; commit(entry, clampTo(Number(range.value), w)); });
          /* Buttons only: the range input is the field, and it already commits on change. */
          wireStepButtons(entry, w, null, function () { return getPath(doc, entry.segs); }, function (v) { commit(entry, v); });
        }
        break;
      case 'text':
        if (editable) {
          var text = q(el, 'input');
          var sendText = function () { entry.dirty = false; commit(entry, text ? text.value : ''); };
          on(text, 'input', function () { entry.dirty = true; });
          on(text, 'keydown', function (ev) { if (isEnter(ev)) { sendText(); } });
          on(q(el, 'send'), 'click', sendText);
        }
        break;
      case 'choice':
        if (editable) {
          var opts = qa(el, 'option');
          for (var i = 0; i < opts.length; i++) {
            (function (index) {
              on(opts[index], 'click', function () {
                if (Array.isArray(w.choices) && w.choices[index]) { commit(entry, w.choices[index].value); }
              });
            })(i);
          }
        }
        break;
      case 'vector3':
        if (editable) { wireComponents(entry, componentNames(w)); }
        break;
      case 'quaternion':
        if (editable) { wireEuler(entry); }
        break;
      case 'table':
        on(q(el, 'prev'), 'click', function () { entry.page -= 1; refresh(entry); });
        on(q(el, 'next'), 'click', function () { entry.page += 1; refresh(entry); });
        break;
      default:
        break;
    }
  }
  function wireComponents(entry, names) {
    var w = entry.w;
    for (var i = 0; i < names.length; i++) {
      (function (name) {
        wireStepper(entry, w, name,
          function () { var v = getPath(doc, entry.segs); return isObject(v) ? v[name] : undefined; },
          function (value) {
            var cur = getPath(doc, entry.segs);
            var next = {};
            for (var k = 0; k < names.length; k++) { next[names[k]] = isObject(cur) && typeof cur[names[k]] === 'number' ? cur[names[k]] : 0; }
            next[name] = value;
            commit(entry, next);
          });
      })(names[i]);
    }
  }
  function wireEuler(entry) {
    var names = ['roll', 'pitch', 'yaw'];
    var spec = { min: -180, max: 180, step: typeof entry.w.step === 'number' ? entry.w.step : 5 };
    for (var i = 0; i < names.length; i++) {
      (function (name) {
        wireStepper(entry, spec, name,
          function () { var v = getPath(doc, entry.segs); return isObject(v) ? quatToEuler(v)[name] : undefined; },
          function (value) {
            var cur = getPath(doc, entry.segs);
            var e = isObject(cur) ? quatToEuler(cur) : { roll: 0, pitch: 0, yaw: 0 };
            e[name] = value;
            commit(entry, eulerToQuat(e));
          });
      })(names[i]);
    }
  }
  function wireButton(entry) {
    var w = entry.w, el = entry.el;
    var button = q(el, 'button');
    var label = q(el, 'text');
    var original = label ? label.textContent : '';
    function disarm() {
      if (entry.armed) { clearTimeout(entry.armed); entry.armed = null; }
      button.removeAttribute('data-qar-armed');
      setText(label, original);
    }
    on(button, 'click', function () {
      if (w.confirm && !entry.armed) {
        /* Two deliberate presses for anything irreversible: a jittering ray must not fire it by accident. */
        setAttr(button, 'data-qar-armed', '1');
        setText(label, 'Press again to confirm');
        entry.armed = setTimeout(disarm, 4000);
        return;
      }
      disarm();
      emitFor(entry, w.payload === undefined ? null : w.payload);
    });
  }
)QARJS";

const char kRuntimeChunk4[] = R"QARJS(
  /* ---- inbound ---- */
  function applyUpdate(msg) {
    if (!isObject(msg) || !Array.isArray(msg.patch)) { return false; }
    var working = clone(doc);
    for (var i = 0; i < msg.patch.length; i++) {
      var entry = msg.patch[i];
      if (!isObject(entry) || !('value' in entry)) { return false; }
      var segs = parsePath(typeof entry.path === 'string' ? entry.path : '');
      if (!segs) { return false; }
      if (segs.length === 0) {
        if (!isObject(entry.value)) { return false; }
        working = clone(entry.value);
        continue;
      }
      if (!setPath(working, segs, entry.value)) { return false; }
    }
    doc = working;
    refreshAll();
    /* New data can change the page's height (a table gains rows, a JSON
       inspector grows), which moves both the pager and what the server
       should know about the content. */
    scheduleViewportReport('content');
    return true;
  }
  function handleMessage(text) {
    var msg;
    try { msg = typeof text === 'string' ? JSON.parse(text) : text; } catch (e) { return false; }
    if (!isObject(msg)) { return false; }
    if (msg.v !== PROTOCOL_VERSION) {
      setStatusLine('error', 'Protocol mismatch: server speaks v' + msg.v + ', this page speaks v' + PROTOCOL_VERSION);
      return false;
    }
    lastFrameAt = Date.now();
    if (state === 'stale' && ws && ws.readyState === 1) { setState('online'); }
    switch (msg.type) {
      case 'hello':
        if (isObject(msg.data)) { doc = clone(msg.data); refreshAll(); }
        return true;
      case 'data':
        return applyUpdate(msg);
      case 'status':
        setStatusLine(typeof msg.level === 'string' ? msg.level : 'neutral', typeof msg.text === 'string' ? msg.text : '');
        return true;
      case 'ack':
        var target = pending[msg.event_seq];
        delete pending[msg.event_seq];
        if (target) { showAck(target, msg); }
        return true;
      case 'ping':
        send({ v: PROTOCOL_VERSION, type: 'pong', seq: ++outSeq, ping_seq: msg.seq });
        return true;
      case 'pong':
        return true;
      default:
        return true; /* unknown types are ignored, per the protocol */
    }
  }

  /* ---- websocket ---- */
  var reconnectTimer = null;
  function resolveUrl() {
    if (typeof config.websocket_url === 'string' && config.websocket_url !== '') { return config.websocket_url; }
    if (!window.location || !window.location.host) { return null; }
    var scheme = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    return scheme + window.location.host + '/ws/' + encodeURIComponent(model.root.id);
  }
  function scheduleReconnect(url) {
    if (reconnectTimer) { return; }
    reconnectTimer = setTimeout(function () { reconnectTimer = null; connect(url); }, config.reconnect_ms > 0 ? config.reconnect_ms : 2000);
  }
  function connect(url) {
    url = url || resolveUrl();
    if (!url || typeof WebSocket === 'undefined') { setState('offline'); return false; }
    if (ws) { try { ws.onclose = null; ws.close(); } catch (e) { /* already gone */ } ws = null; }
    var socket;
    try { socket = new WebSocket(url); } catch (e) { setState('offline'); scheduleReconnect(url); return false; }
    ws = socket;
    socket.onopen = function () {
      lastFrameAt = Date.now();
      setState('online');
      send({ v: PROTOCOL_VERSION, type: 'hello', seq: ++outSeq, ts: Date.now() / 1000, panel: model.root.id, runtime: RUNTIME });
      /* The server sizes the panel from this. A reconnect re-reports: the
         server on the other end may not be the one that last heard. */
      lastViewportSent = null;
      scheduleViewportReport('connect');
    };
    socket.onmessage = function (ev) { handleMessage(ev.data); };
    socket.onclose = function () { if (ws === socket) { ws = null; } setState('offline'); scheduleReconnect(url); };
    socket.onerror = function () { /* onclose follows */ };
    return true;
  }
  function disconnect() {
    if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
    if (ws) { var s = ws; ws = null; s.onclose = null; try { s.close(); } catch (e) { /* ignore */ } }
    setState('offline');
  }
  setInterval(function () {
    var now = Date.now();
    if (ws && ws.readyState === 1) {
      var pingEvery = config.ping_interval_ms > 0 ? config.ping_interval_ms : 5000;
      if (now - lastSentAt >= pingEvery && now - lastFrameAt >= pingEvery) { send({ v: PROTOCOL_VERSION, type: 'ping', seq: ++outSeq }); }
      var staleAfter = config.stale_after_ms > 0 ? config.stale_after_ms : 5000;
      if (state === 'online' && now - lastFrameAt > staleAfter) { setState('stale'); }
    }
  }, 1000);

)QARJS";

const char kRuntimeChunk5[] = R"QARJS(
  /* ---- the viewport: fitting to it, reporting it, and paging what overflows
     Mirrors qar_ui_model/viewport_fit.hpp and scroll_paging.hpp exactly;
     change one, change both. Three jobs, one measurement:
       1. FIT the design to the viewport we were actually given. The QarOS
          runtime hands this page a fixed 500x500 CSS px box whatever metres
          the panel declares, and a page drawn for 756 px does not overflow in
          it - it SHRINKS, until the text collides and the buttons touch. So
          the page scales its own theme down to fit and the server grows the
          panel by the same factor in metres, which leaves every angular size
          exactly where it was. Done here rather than server-side because the
          browser is the only party that knows the viewport at first paint.
       2. Tell the server the CSS viewport this page was ACTUALLY given. The
          panel's physical size is committed before the browser lays anything
          out, and nothing in the QarOS SDK says how the viewport follows from
          it (docs/SDK_COMPATIBILITY.md, "The QarOS in-AR browser"), so the page
          reports and the server resizes. Without this the panel's aspect is a
          guess, and a wrong guess is a non-uniform stretch - a horizontal
          squeeze.
       3. Decide whether the click pager is needed, and drive it. */
  var OVERFLOW_TOLERANCE_PX = 4;
  var PAGER_EXIT_MARGIN_PX = 32;
  var PAGE_OVERLAP_FRACTION = 0.12;
  var MIN_PAGE_STEP_FRACTION = 0.5;
  var pagerRail = null;
  var pagerOn = false;
  var lastViewportSent = null;
  var viewportTimer = null;

  function panelEl() { return document.querySelector('.qar-panel'); }
  function docEl() { return document.documentElement || document.body; }
  /* When the pager is up the panel is the scroller (see the CSS note: the rail
     is absolute inside a body that does not scroll, because this engine has no
     evidenced position:fixed). Otherwise the document scrolls, as ever. */
  function scroller() {
    if (pagerOn) { var p = panelEl(); if (p) { return p; } }
    return document.scrollingElement || docEl() || document.body;
  }
  function scrollTopOf(el) { return el ? num(el.scrollTop) : 0; }
  function viewportHeight() {
    if (pagerOn) { var p = panelEl(); if (p && p.clientHeight > 0) { return p.clientHeight; } }
    if (num(window.innerHeight) > 0) { return num(window.innerHeight); }
    var d = docEl();
    return d ? num(d.clientHeight) : 0;
  }
  function viewportWidth() {
    if (num(window.innerWidth) > 0) { return num(window.innerWidth); }
    var d = docEl();
    return d ? num(d.clientWidth) : 0;
  }

  /* The fit. config.fit carries what the browser cannot measure for itself:
     how wide this page wants to be at one, two and three columns (measured
     server-side by qar_ui_model::natural_page_widths), what the pager rail
     costs the content column, and every pixel size the stylesheet reads from
     :root. Columns first, because dropping one is cheaper than shrinking; the
     scale is then one division, not a search, since the rail scales with the
     theme too. */
  var fitConfig = isObject(config.fit) ? config.fit : null;
  var appliedFit = null;

  function fitNaturalWidths() {
    var list = fitConfig ? fitConfig.natural_w : null;
    return (list && typeof list.length === 'number' && list.length > 0) ? list : null;
  }
  function fitColumns(viewport) {
    var natural = fitNaturalWidths();
    if (!natural || !(viewport > 0)) { return 1; }
    var reserve = num(fitConfig.reserve_px);
    var columns = 1;
    for (var i = 1; i < natural.length; i++) {
      if (num(natural[i]) + reserve > viewport) { break; }
      columns = i + 1;
    }
    return columns;
  }
  function fitMinScale() {
    var maxScale = fitConfig && num(fitConfig.max_scale) > 0 ? num(fitConfig.max_scale) : 1;
    var minScale = fitConfig ? num(fitConfig.min_scale) : 0;
    return (minScale > 0 && minScale <= maxScale) ? minScale : maxScale;
  }
  function fitScale(naturalWidth, viewport) {
    var maxScale = fitConfig && num(fitConfig.max_scale) > 0 ? num(fitConfig.max_scale) : 1;
    if (!(naturalWidth > 0) || !(viewport > 0)) { return maxScale; }
    var fit = viewport / (naturalWidth + num(fitConfig.reserve_px));
    return Math.min(Math.max(fit, fitMinScale()), maxScale);
  }
  /* Snapped DOWN onto the whole-pixel body font, which is the only grid the
     scale can actually land on. Down rather than nearest is what makes the fit
     a guarantee rather than an estimate, and snapping at all is what lets the
     server invert the same number exactly when it sizes the panel in metres. */
  function fitQuantise(scale, baseFont) {
    if (!(baseFont > 0) || !isFinite(scale)) { return 1; }
    var font = Math.floor(baseFont * scale + 1e-9);
    var floorFont = Math.ceil(baseFont * fitMinScale() - 1e-9);
    font = Math.max(font, Math.max(1, floorFont));
    font = Math.min(font, baseFont);
    return font / baseFont;
  }
  function applyFit(reason) {
    if (!fitConfig) { return null; }
    var root = docEl();
    if (!root || !root.style || !root.style.setProperty) { return null; }
    var viewport = viewportWidth();
    if (!(viewport > 0)) { return null; }
    var columns = fitColumns(viewport);
    var natural = fitNaturalWidths();
    var vars = isObject(fitConfig.vars) ? fitConfig.vars : {};
    var scale = fitQuantise(
      fitScale(natural ? num(natural[columns - 1]) : 0, viewport), num(vars['--qar-font']));
    /* A pixel of resize noise must not restyle the document. The scale cannot
       oscillate on its own - restyling does not change the viewport the
       runtime gives us, so there is no feedback path - but a resize storm can
       still arrive, and settling once is the whole intent. */
    if (appliedFit && appliedFit.columns === columns &&
      Math.abs(scale - appliedFit.scale) <= num(fitConfig.deadband)) { return appliedFit; }
    for (var name in vars) {
      if (!Object.prototype.hasOwnProperty.call(vars, name)) { continue; }
      /* Mirrors ArTheme::scaled: round, never below one pixel. */
      root.style.setProperty(name, Math.max(1, Math.round(num(vars[name]) * scale)) + 'px');
    }
    /* One column means one child per line whatever the basis rounds to: a row
       whose children each ask for the full width cannot put two side by side,
       and two 260 px columns in a 500 px viewport is what collided the
       buttons in the first place. */
    if (columns === 1) { root.style.setProperty('--qar-col', '100%'); }
    appliedFit = { scale: scale, columns: columns, viewport: viewport, reason: reason || 'boot' };
    return appliedFit;
  }

  function planPaging(viewport, content, scroll) {
    viewport = (isFinite(viewport) && viewport > 0) ? viewport : 0;
    content = (isFinite(content) && content > 0) ? content : 0;
    var plan = { needed: false, pages: 1, page: 1, step: viewport, max: Math.max(0, content - viewport) };
    if (!(viewport > 0) || !(content > viewport + OVERFLOW_TOLERANCE_PX)) {
      plan.label = '1 / 1';
      plan.aria = 'Page 1 of 1';
      return plan;
    }
    plan.needed = true;
    plan.step = Math.max(viewport * (1 - PAGE_OVERLAP_FRACTION), viewport * MIN_PAGE_STEP_FRACTION);
    var steps = Math.ceil(plan.max / plan.step - 1e-9);
    plan.pages = Math.max(1, steps) + 1;
    scroll = Math.min(Math.max((isFinite(scroll) && scroll > 0) ? scroll : 0, 0), plan.max);
    /* The last page is claimed by reaching the bottom, not by crossing a step
       boundary: the final step is short, and "2 of 3" at the end of the
       document would be a lie. */
    var page = scroll >= plan.max - 1 ? plan.pages : Math.floor(scroll / plan.step + 1e-9) + 1;
    plan.page = Math.max(1, Math.min(page, plan.pages));
    plan.label = plan.page + ' / ' + plan.pages;
    plan.aria = 'Page ' + plan.page + ' of ' + plan.pages;
    return plan;
  }

  function setPagerMode(on) {
    if (on === pagerOn) { return; }
    pagerOn = on;
    var root = docEl();
    if (root) {
      if (on) { root.setAttribute('data-qar-pager', '1'); } else { root.removeAttribute('data-qar-pager'); }
    }
    if (pagerRail) { pagerRail.hidden = !on; }
  }

  function updatePager() {
    if (!pagerRail) { return null; }
    /* Hysteresis, because turning the rail ON narrows the content column and
       can reflow the page taller: leaving paged mode needs clear air. */
    var el = scroller();
    var view = viewportHeight();
    var content = el ? num(el.scrollHeight) : 0;
    if (!pagerOn) {
      var d = docEl();
      var pageContent = d ? Math.max(num(d.scrollHeight), content) : content;
      if (view > 0 && pageContent > view + OVERFLOW_TOLERANCE_PX) { setPagerMode(true); }
    } else if (view > 0 && content <= view - PAGER_EXIT_MARGIN_PX) {
      setPagerMode(false);
    }
    el = scroller();
    view = viewportHeight();
    var plan = planPaging(view, el ? num(el.scrollHeight) : 0, scrollTopOf(el));
    setText(document.getElementById('qar-scroll-pos'), plan.label);
    setAttr(document.getElementById('qar-scroll-pos'), 'aria-label', plan.aria);
    return plan;
  }

  function scrollToOffset(target) {
    var el = scroller();
    if (!el) { return; }
    var plan = planPaging(viewportHeight(), num(el.scrollHeight), scrollTopOf(el));
    el.scrollTop = Math.min(Math.max(target, 0), plan.max);
    updatePager();
    scheduleViewportReport('content');
  }
  function pageBy(direction) {
    var el = scroller();
    if (!el) { return; }
    var plan = planPaging(viewportHeight(), num(el.scrollHeight), scrollTopOf(el));
    scrollToOffset(scrollTopOf(el) + direction * plan.step);
  }

  function measureViewport(reason) {
    var d = docEl();
    var panel = panelEl();
    var frame = {
      v: PROTOCOL_VERSION, type: 'viewport', seq: 0, ts: Date.now() / 1000,
      css_w: num(window.innerWidth) || (d ? num(d.clientWidth) : 0),
      css_h: num(window.innerHeight) || (d ? num(d.clientHeight) : 0),
      reason: reason || 'connect'
    };
    var dpr = num(window.devicePixelRatio);
    if (dpr > 0) { frame.dpr = dpr; }
    if (d) {
      if (num(d.scrollWidth) > 0) { frame.content_w = num(d.scrollWidth); }
      if (num(d.scrollHeight) > 0) { frame.content_h = num(d.scrollHeight); }
    }
    if (panel) {
      /* Once the pager owns the scrolling, documentElement reports a viewport
         and only the scroller knows how much page there really is. */
      if (pagerOn && num(panel.scrollHeight) > 0) { frame.content_h = num(panel.scrollHeight); }
      if (num(panel.offsetWidth) > 0) { frame.root_w = num(panel.offsetWidth); }
      if (num(panel.offsetHeight) > 0) { frame.root_h = num(panel.offsetHeight); }
    }
    return frame;
  }
  /* Unchanged numbers are not news: a resize storm, or a data frame that did
     not move the layout, must not become a frame storm on the socket. */
  function viewportChanged(frame) {
    if (!lastViewportSent) { return true; }
    var keys = ['css_w', 'css_h', 'dpr', 'content_w', 'content_h', 'root_w', 'root_h'];
    for (var i = 0; i < keys.length; i++) {
      if (Math.abs(num(frame[keys[i]]) - num(lastViewportSent[keys[i]])) > 1) { return true; }
    }
    return false;
  }
  function reportViewport(reason) {
    var frame = measureViewport(reason);
    if (!(frame.css_w > 0) || !(frame.css_h > 0)) { return false; }
    if (!viewportChanged(frame)) { return false; }
    frame.seq = ++outSeq;
    if (!send(frame)) { return false; }
    lastViewportSent = frame;
    return true;
  }
  /* Layout settles a tick after a change; measuring in the same turn reads the
     old box. One trailing timer covers resize, data and boot alike. */
  function scheduleViewportReport(reason) {
    if (viewportTimer) { clearTimeout(viewportTimer); }
    viewportTimer = setTimeout(function () {
      viewportTimer = null;
      updatePager();
      reportViewport(reason);
    }, 120);
  }

  function wirePager() {
    pagerRail = document.getElementById('qar-scroll');
    if (!pagerRail) { return; }
    function on(role, fn) {
      var btn = q(pagerRail, role);
      /* click only, and nothing here may depend on hover. One click, one
         complete jump. (AR gained a held press in the 2026-09-21 SDK, but a
         website panel's contacts are handled by the renderer's own browser,
         and a page rail wants discrete jumps regardless.) */
      if (btn) { btn.addEventListener('click', fn); }
    }
    on('scroll-top', function () { scrollToOffset(0); });
    on('scroll-up', function () { pageBy(-1); });
    on('scroll-down', function () { pageBy(1); });
    on('scroll-bottom', function () { scrollToOffset(1e9); });
    var panel = panelEl();
    if (panel) { panel.addEventListener('scroll', function () { updatePager(); }); }
    window.addEventListener('scroll', function () { updatePager(); });
    /* The fit first: it changes the layout, so measuring before it would page
       the old page. The report that follows re-measures both. */
    window.addEventListener('resize', function () { applyFit('resize'); updatePager(); scheduleViewportReport('resize'); });
    updatePager();
  }

)QARJS";

// MSVC refuses a string literal over 16380 characters, so the runtime is
// emitted in chunks; the split points are arbitrary and the concatenation in
// runtime_javascript() is the only thing that matters.
const char kRuntimeChunk6[] = R"QARJS(
  /* ---- capability probe ----
     The headset browser is Ultralight, a reduced WebKit, not Chromium; the
     feature matrix is in docs/SDK_COMPATIBILITY.md. None of
     this changes what a capable engine does. It exists so that an engine missing
     something the panel needs says which thing, in the panel, in words. */
  function probeCssVariables() {
    try {
      var el = document.createElement('div');
      el.style.position = 'absolute';
      el.style.setProperty('--qar-probe', '7px');
      el.style.width = 'var(--qar-probe)';
      document.body.appendChild(el);
      var w = el.offsetWidth;
      document.body.removeChild(el);
      return w === 7;
    } catch (e) { return true; }  /* cannot measure: do not cry wolf */
  }
  /* The gap property parses wherever CSS grid exists, but WebKit only applies it
     to flex containers from 611 (Safari 14.1), and Ultralight 1.3 reports 610.
     Measure it, do not sniff: two 1px rows, 11px of row-gap, 13px or 2px. */
  function probeFlexGap() {
    try {
      var outer = document.createElement('div');
      outer.style.position = 'absolute';
      outer.style.visibility = 'hidden';
      outer.style.left = '-9999px';
      outer.style.display = 'flex';
      outer.style.flexDirection = 'column';
      outer.style.rowGap = '11px';
      for (var i = 0; i < 2; i++) {
        var kid = document.createElement('div');
        kid.style.height = '1px';
        outer.appendChild(kid);
      }
      document.body.appendChild(outer);
      var h = outer.offsetHeight;
      document.body.removeChild(outer);
      return h >= 12;
    } catch (e) { return true; }
  }
  function missingCapabilities(needsSvg) {
    var missing = [];
    function need(ok, name) { if (!ok) { missing.push(name); } }
    need(typeof JSON !== 'undefined' && typeof JSON.parse === 'function', 'JSON');
    need(typeof document.querySelector === 'function', 'querySelector');
    need(typeof document.createElement('div').addEventListener === 'function', 'addEventListener');
    need(typeof setTimeout === 'function' && typeof setInterval === 'function', 'setTimeout/setInterval');
    need(typeof WebSocket !== 'undefined', 'WebSocket (live data and controls)');
    need(probeCssVariables(), 'CSS custom properties');
    if (needsSvg) { need(typeof window.SVGPolylineElement !== 'undefined', 'inline SVG (chart widgets)'); }
    return missing;
  }
  var diagBox = document.getElementById('qar-diag');
  /* The one message an operator in a headset can act on. */
  function report(title, detail) {
    if (!diagBox) { return; }
    setText(document.getElementById('qar-diag-title'), title);
    setText(document.getElementById('qar-diag-detail'), detail);
    diagBox.hidden = false;
  }
  function engineName() {
    return (typeof navigator !== 'undefined' && navigator.userAgent) ? navigator.userAgent : 'unknown';
  }

  /* ---- boot ---- */
  try {
    /* Before anything is registered or drawn: the theme scale is a restyle of
       :root, and doing it first means the first paint is already the fitted
       one rather than a frame of the squeezed layout. */
    applyFit('boot');
    register(model.root);
    refreshAll();
    var hasChart = false;
    for (var hi = 0; hi < entries.length; hi++) { if (entries[hi].w.type === 'chart') { hasChart = true; } }
    if (hasChart) {
      setInterval(function () { for (var i = 0; i < entries.length; i++) { if (entries[i].w.type === 'chart') { drawChart(entries[i]); } } }, 500);
    }
    if (!probeFlexGap() && document.documentElement) {
      document.documentElement.setAttribute('data-qar-nogap', '1');
    }
    wirePager();
    scheduleViewportReport('connect');
    var absent = missingCapabilities(hasChart);
    if (absent.length) {
      report('This browser is missing ' + absent.length + ' feature' + (absent.length === 1 ? '' : 's') + ' the panel needs',
        'Not supported here: ' + absent.join(', ') + '. Engine: ' + engineName() + '.');
    } else if (diagBox) {
      diagBox.hidden = true;
    }
  } catch (bootError) {
    report('The panel runtime stopped while starting up',
      String((bootError && bootError.message) ? bootError.message : bootError) + ' Engine: ' + engineName() + '.');
  }

  /* The public runtime contract (see docs/WIRE_PROTOCOL.md, "Page runtime API"). */
  window.QarPanel = {
    protocolVersion: PROTOCOL_VERSION,
    runtime: RUNTIME,
    model: model,
    getData: function () { return doc; },
    getValue: function (path) { var s = parsePath(path); return s ? getPath(doc, s) : undefined; },
    setValue: function (path, value) { var s = parsePath(path); if (!s) { return false; } if (s.length === 0) { if (!isObject(value)) { return false; } doc = clone(value); } else if (!setPath(doc, s, value)) { return false; } refreshAll(); return true; },
    applyUpdate: applyUpdate,
    handleMessage: handleMessage,
    emit: emit,
    onEvent: function (fn) { if (typeof fn === 'function') { listeners.push(fn); } },
    connect: connect,
    disconnect: disconnect,
    state: function () { return state; },
    refresh: refreshAll,
    parsePath: parsePath,
    /* Paging and the viewport report, so a harness can drive them without
       a hand ray. planPaging mirrors qar_ui_model::plan_paging. */
    planPaging: planPaging,
    pageBy: pageBy,
    scrollToOffset: scrollToOffset,
    pagerState: updatePager,
    viewport: measureViewport,
    reportViewport: reportViewport,
    /* The viewport fit, so a harness can drive it and read back what was
       chosen. applyFit mirrors qar_ui_model::fit_page_to_viewport. */
    applyFit: applyFit,
    fitState: function () { return appliedFit; }
  };

  try {
    connect();
  } catch (connectError) {
    report('The panel could not open its live connection',
      String((connectError && connectError.message) ? connectError.message : connectError) + ' Engine: ' + engineName() + '.');
  }
})();
)QARJS";

}  // namespace

std::string
runtime_javascript()
{
  std::string out;
  out.reserve(sizeof(kRuntimeChunk1) + sizeof(kRuntimeChunk2) + sizeof(kRuntimeChunk3) +
    sizeof(kRuntimeChunk4) + sizeof(kRuntimeChunk5) + sizeof(kRuntimeChunk6));
  out += kRuntimeChunk1;
  out += kRuntimeChunk2;
  out += kRuntimeChunk3;
  out += kRuntimeChunk4;
  out += kRuntimeChunk5;
  out += kRuntimeChunk6;
  return out;
}

}  // namespace qar_ui_model

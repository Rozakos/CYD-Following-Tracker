#pragma once

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>

// Follower-count formatting helpers. Counts move slowly and almost always
// rise, so the UI leads with compact magnitudes (1.2M) and grouped absolute
// deltas (+1,240) rather than percentages — see AGENTS.md "UI rationale".
namespace fmt {

// 1234 -> "1,234", 1234567 -> "1,234,567". Handles negatives.
inline String grouped(long long n) {
  bool neg = n < 0;
  unsigned long long v = neg ? (unsigned long long)(-n) : (unsigned long long)n;
  char digits[24];
  int len = std::snprintf(digits, sizeof(digits), "%llu", v);
  String out;
  if (neg) out += '-';
  for (int i = 0; i < len; ++i) {
    if (i && (len - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return out;
}

// Compact magnitude: 936 -> "936", 12345 -> "12.3K", 3450000 -> "3.4M",
// 1250000000 -> "1.2B". One decimal below 100 of each unit, none above, and
// trailing ".0" trimmed.
inline String compact(long long n) {
  bool neg = n < 0;
  double v = neg ? -(double)n : (double)n;
  const char* unit = "";
  double scaled = v;
  if (v >= 1e9)      { scaled = v / 1e9; unit = "B"; }
  else if (v >= 1e6) { scaled = v / 1e6; unit = "M"; }
  else if (v >= 1e3) { scaled = v / 1e3; unit = "K"; }
  else {
    return String(neg ? "-" : "") + String((long)v);
  }
  char buf[24];
  if (scaled >= 100.0) std::snprintf(buf, sizeof(buf), "%.0f%s", scaled, unit);
  else                 std::snprintf(buf, sizeof(buf), "%.1f%s", scaled, unit);
  // Trim a trailing ".0" (e.g. "3.0M" -> "3M").
  String s(buf);
  int dot = s.indexOf('.');
  if (dot >= 0 && dot + 2 < (int)s.length() &&
      s[dot + 1] == '0' && (s[dot + 2] < '0' || s[dot + 2] > '9')) {
    s = s.substring(0, dot) + s.substring(dot + 2);
  }
  return String(neg ? "-" : "") + s;
}

// Signed grouped delta: 1240 -> "+1,240", -87 -> "-87", 0 -> "0".
inline String delta(long long d) {
  if (d > 0) return String("+") + grouped(d);
  return grouped(d);  // grouped() already prefixes '-' for negatives
}

}  // namespace fmt

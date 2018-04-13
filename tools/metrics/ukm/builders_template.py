# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Templates for generating builder classes for UKM entries."""

import codegen

HEADER = codegen.Template(
basename="ukm_builders.h",
file_template="""
// Generated from gen_builders.py.  DO NOT EDIT!
// source: ukm.xml

#ifndef {file.guard_path}
#define {file.guard_path}

#include <cstdint>

#include "services/metrics/public/cpp/ukm_entry_builder_base.h"

namespace ukm {{
namespace builders {{

{event_code}

}}  // namespace builders
}}  // namespace ukm

#endif  // {file.guard_path}
""",
event_template="""
class {event.name} : public ::ukm::internal::UkmEntryBuilderBase {{
 public:
  {event.name}(ukm::SourceId source_id);
  ~{event.name}() override;

  static const char kEntryName[];
  static constexpr uint64_t kEntryNameHash = UINT64_C({event.hash});

{metric_code}
}};
""",
metric_template="""
  static const char k{metric.name}Name[];
  static constexpr uint64_t k{metric.name}NameHash = UINT64_C({metric.hash});
  {event.name}& Set{metric.name}(int64_t value);
""")

IMPL = codegen.Template(
basename="ukm_builders.cc",
file_template="""
// Generated from gen_builders.py.  DO NOT EDIT!
// source: ukm.xml

#include "{file.dir_path}/ukm_builders.h"

namespace ukm {{
namespace builders {{

{event_code}

}}  // namespace builders
}}  // namespace ukm
""",
event_template="""
const char {event.name}::kEntryName[] = "{event.raw_name}";

{event.name}::{event.name}(ukm::SourceId source_id) :
  ::ukm::internal::UkmEntryBuilderBase(source_id, kEntryNameHash) {{
}}

{event.name}::~{event.name}() = default;

{metric_code}
""",
metric_template="""
const char {event.name}::k{metric.name}Name[] = "{metric.raw_name}";

{event.name}& {event.name}::Set{metric.name}(int64_t value) {{
  AddMetric(k{metric.name}NameHash, value);
  return *this;
}}
""")


def WriteFiles(outdir, relpath, data):
  HEADER.WriteFile(outdir, relpath, data)
  IMPL.WriteFile(outdir, relpath, data)

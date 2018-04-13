// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cycle_sub.h"

namespace blink {

void B::Trace(Visitor* visitor) {
    visitor->Trace(m_c);
    A::Trace(visitor);
}

}

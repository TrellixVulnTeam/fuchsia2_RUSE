// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#pragma once

//
//
//

#include "common/macros.h"

//
//
//

#if 1

#include <assert.h>

#define SPN_ASSERT_STATE_DECLARE(type)          type state
#define SPN_ASSERT_STATE_MEMBER(sp)             (sp)->state
#define SPN_ASSERT_STATE_INIT(sp,to)            SPN_ASSERT_STATE_MEMBER(sp) = (to)
#define SPN_ASSERT_STATE_TRANSITION(from,to,sp) assert(SPN_ASSERT_STATE_MEMBER(sp) == (from)); SPN_ASSERT_STATE_INIT(sp,to)
#define SPN_ASSERT_STATE_ASSERT(at,sp)          assert(SPN_ASSERT_STATE_MEMBER(sp) == (at))

#else

#define SPN_ASSERT_STATE_DECLARE(st)
#define SPN_ASSERT_STATE_INIT(sp,to)
#define SPN_ASSERT_STATE_TRANSITION(from,to,sp)
#define SPN_ASSERT_STATE_ASSERT(at,sp)

#endif

//
//
//

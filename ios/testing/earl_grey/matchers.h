// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_TESTING_EARL_GREY_MATCHERS_H_
#define IOS_TESTING_EARL_GREY_MATCHERS_H_

#import <Foundation/Foundation.h>

#import <EarlGrey/EarlGrey.h>

namespace testing {

// Matcher for element with accessibility label corresponding to |label| and
// accessibility trait UIAccessibilityTraitButton.
id<GREYMatcher> ButtonWithAccessibilityLabel(NSString* label);

// Matcher for a UI element to tap to dismiss an alert (e.g. context menu),
// where |cancel_text| is the localized text used for the action sheet cancel
// control.
// On phones, where the alert is an action sheet, this will be a matcher for the
// menu item with |cancel_text| as its label.
// On tablets, where the alert is a popover, this will be a matcher for some
// element outside of the popover.
id<GREYMatcher> ElementToDismissAlert(NSString* cancel_text);

}  // namespace testing

#endif  // IOS_TESTING_EARL_GREY_MATCHERS_H_

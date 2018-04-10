/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "WebRTC/RTCMTLVideoView.h"

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#import "WebRTC/RTCLogging.h"
#import "WebRTC/RTCVideoFrame.h"
#import "WebRTC/RTCVideoFrameBuffer.h"

#import "RTCMTLI420Renderer.h"
#import "RTCMTLNV12Renderer.h"

// To avoid unreconized symbol linker errors, we're taking advantage of the objc runtime.
// Linking errors occur when compiling for architectures that don't support Metal.
#define MTKViewClass NSClassFromString(@"MTKView")
#define RTCMTLNV12RendererClass NSClassFromString(@"RTCMTLNV12Renderer")
#define RTCMTLI420RendererClass NSClassFromString(@"RTCMTLI420Renderer")

@interface RTCMTLVideoView () <MTKViewDelegate>
@property(nonatomic, strong) RTCMTLI420Renderer *rendererI420;
@property(nonatomic, strong) RTCMTLNV12Renderer *rendererNV12;
@property(nonatomic, strong) MTKView *metalView;
@property(atomic, strong) RTCVideoFrame *videoFrame;
@end

@implementation RTCMTLVideoView

@synthesize rendererI420 = _rendererI420;
@synthesize rendererNV12 = _rendererNV12;
@synthesize metalView = _metalView;
@synthesize videoFrame = _videoFrame;

- (instancetype)initWithFrame:(CGRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self) {
    [self configure];
  }
  return self;
}

- (instancetype)initWithCoder:(NSCoder *)aCoder {
  self = [super initWithCoder:aCoder];
  if (self) {
    [self configure];
  }
  return self;
}

#pragma mark - Private

+ (BOOL)isMetalAvailable {
#if defined(RTC_SUPPORTS_METAL)
  return YES;
#else
  return NO;
#endif
}

+ (MTKView *)createMetalView:(CGRect)frame {
  MTKView *view = [[MTKViewClass alloc] initWithFrame:frame];
  return view;
}

+ (RTCMTLNV12Renderer *)createNV12Renderer {
  return [[RTCMTLNV12RendererClass alloc] init];
}

+ (RTCMTLI420Renderer *)createI420Renderer {
  return [[RTCMTLI420RendererClass alloc] init];
}

- (void)configure {
  NSAssert([RTCMTLVideoView isMetalAvailable], @"Metal not availiable on this device");

  _metalView = [RTCMTLVideoView createMetalView:self.bounds];
  [self configureMetalView];
}

- (void)configureMetalView {
  if (_metalView) {
    _metalView.delegate = self;
    [self addSubview:_metalView];
    _metalView.contentMode = UIViewContentModeScaleAspectFill;
  }
}

#pragma mark - Private

- (void)layoutSubviews {
  [super layoutSubviews];
  _metalView.frame = self.bounds;
}

#pragma mark - MTKViewDelegate methods

- (void)drawInMTKView:(nonnull MTKView *)view {
  NSAssert(view == self.metalView, @"Receiving draw callbacks from foreign instance.");
  RTCVideoFrame *videoFrame = self.videoFrame;
  if (!videoFrame) {
    return;
  }

  if ([videoFrame.buffer isKindOfClass:[RTCCVPixelBuffer class]]) {
    if (!self.rendererNV12) {
      self.rendererNV12 = [RTCMTLVideoView createNV12Renderer];
      if (![self.rendererNV12 addRenderingDestination:self.metalView]) {
        self.rendererNV12 = nil;
        RTCLogError(@"Failed to create NV12 renderer");
      }
    }
    [self.rendererNV12 drawFrame:videoFrame];
  } else {
    if (!self.rendererI420) {
      self.rendererI420 = [RTCMTLVideoView createI420Renderer];
      if (![self.rendererI420 addRenderingDestination:self.metalView]) {
        self.rendererI420 = nil;
        RTCLogError(@"Failed to create I420 renderer");
      }
    }
    [self.rendererI420 drawFrame:videoFrame];
  }
  [self updateVideoOrientation];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
}

#pragma mark - RTCVideoRenderer

- (void)setSize:(CGSize)size {
  self.metalView.drawableSize = size;
  [self updateVideoOrientation];
}

- (void)renderFrame:(nullable RTCVideoFrame *)frame {
  if (frame == nil) {
    RTCLogInfo(@"Incoming frame is nil. Exiting render callback.");
    return;
  }
  self.videoFrame = frame;
  [self updateVideoOrientation];
}

- (void)updateVideoOrientation {
  RTCVideoFrame *videoFrame = self.videoFrame;
  if (!videoFrame) {
    return;
  }
  // If the video is in a different orientation that the device, change the
  // content mode to fit.
  BOOL isLandscape = NO;
  switch ([UIApplication sharedApplication].statusBarOrientation) {
    case UIInterfaceOrientationPortraitUpsideDown:
    case UIInterfaceOrientationPortrait:
    case UIInterfaceOrientationUnknown:
      isLandscape = NO;
      break;
    case UIInterfaceOrientationLandscapeLeft:
    case UIInterfaceOrientationLandscapeRight:
      isLandscape = YES;
      break;
  }
  UIViewContentMode contentMode = _metalView.contentMode;
  switch (videoFrame.rotation) {
    case RTCVideoRotation_0:
    case RTCVideoRotation_180:
      // Landscape.
      if (isLandscape) {
        contentMode = UIViewContentModeScaleAspectFill;
      } else {
        contentMode = UIViewContentModeScaleAspectFit;
      }
      break;
    case RTCVideoRotation_90:
    case RTCVideoRotation_270:
      // Portrait.
      if (isLandscape) {
        contentMode = UIViewContentModeScaleAspectFit;
      } else {
        contentMode = UIViewContentModeScaleAspectFill;
      }
      break;
  }
  if (_metalView.contentMode != contentMode) {
    dispatch_async(dispatch_get_main_queue(), ^{
      _metalView.contentMode = contentMode;
    });
  }
}

@end

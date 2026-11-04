/** @file test_stream_ctrl.cc
 * @brief Tests for uvc_get_stream_ctrl_format_size() in src/stream.c.
 *
 * The function picks a format, frame size and interval from the device's
 * descriptors, which are device-supplied, then negotiates them with the
 * camera. There is no camera here: libusb_control_transfer() is replaced
 * below, and fails every request, which leaves the stream control as the
 * function filled it in. The descriptors are parsed from bytes, as in
 * test_descriptor.cc.
 */
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "uvc_test_util.h"

/* libuvc is linked in statically, so this takes precedence over libusb's. */
extern "C" int LIBUSB_CALL libusb_control_transfer(
    libusb_device_handle *, uint8_t, uint8_t, uint16_t, uint16_t,
    unsigned char *, uint16_t, unsigned int) {
  return LIBUSB_ERROR_PIPE;
}

namespace {

constexpr uint32_t k30Fps = 333333, k10Fps = 1000000;

/** A device handle whose one VideoStreaming interface offers YUYV 640x480,
 * at any frame interval from 1/30 s to 1/10 s by @p step (100 ns units). */
class ContinuousFrameFixture {
 public:
  explicit ContinuousFrameFixture(uint32_t step) {
    std::vector<unsigned char> extra = {
      /* VS input header */
      0x0e, 0x24, 0x01, 0x01, 0x00, 0x00, 0x81, 0x00,
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
      /* Uncompressed format, bFormatIndex 1, 1 frame descriptor */
      0x1b, 0x24, 0x04, 0x01, 0x01,
      'Y', 'U', 'Y', '2', 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa,
      0x00, 0x38, 0x9b, 0x71, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00,
      /* Uncompressed frame, 640x480, up to dwMaxVideoFrameBufferSize */
      0x26, 0x24, 0x05, 0x01, 0x00, 0x80, 0x02, 0xe0, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x60, 0x09, 0x00,
    };
    AppendDword(&extra, k30Fps);  /* dwDefaultFrameInterval */
    extra.push_back(0);           /* bFrameIntervalType: continuous */
    AppendDword(&extra, k30Fps);  /* dwMinFrameInterval */
    AppendDword(&extra, k10Fps);  /* dwMaxFrameInterval */
    AppendDword(&extra, step);    /* dwFrameIntervalStep */

    uvc_test_config_init(&tc_, 1);
    uvc_test_config_set_extra(&tc_, 0, extra.data(), (int)extra.size());
    uvc_test_info_init(&info_, &tc_);
    EXPECT_EQ(uvc_scan_streaming(&info_, 0), UVC_SUCCESS);

    std::memset(&devh_, 0, sizeof(devh_));
    devh_.info = &info_;
    /* Already claimed, so that uvc_claim_if() does not call libusb. */
    devh_.claimed = 1 << 0;
  }

  ~ContinuousFrameFixture() {
    uvc_test_info_free(&info_);
    uvc_test_config_free(&tc_);
  }

  uvc_error_t Get(int fps, uvc_stream_ctrl_t *ctrl) {
    std::memset(ctrl, 0, sizeof(*ctrl));
    return uvc_get_stream_ctrl_format_size(&devh_, ctrl, UVC_FRAME_FORMAT_YUYV,
                                           640, 480, fps);
  }

 private:
  static void AppendDword(std::vector<unsigned char> *v, uint32_t x) {
    for (int i = 0; i < 4; ++i)
      v->push_back((unsigned char)(x >> (8 * i)));
  }

  uvc_test_config_t tc_;
  uvc_device_info_t info_;
  uvc_device_handle_t devh_;
};

/* A rate in the range and on the step is picked, one off the step is not. */
TEST(StreamCtrlFormatSize, ContinuousIntervalsFollowTheStep) {
  ContinuousFrameFixture f(k30Fps);
  uvc_stream_ctrl_t ctrl;

  EXPECT_EQ(f.Get(30, &ctrl), UVC_SUCCESS);
  EXPECT_EQ(ctrl.dwFrameInterval, k30Fps);
  EXPECT_EQ(f.Get(15, &ctrl), UVC_SUCCESS);
  EXPECT_EQ(ctrl.dwFrameInterval, 2 * k30Fps);
  EXPECT_EQ(f.Get(20, &ctrl), UVC_ERROR_INVALID_MODE);
}

/* fps 0 asks for the first rate available, as it does for discrete
 * intervals: the shortest interval. It divided 10000000 by it instead. */
TEST(StreamCtrlFormatSize, AnyRateFromContinuousIntervals) {
  ContinuousFrameFixture f(k30Fps);
  uvc_stream_ctrl_t ctrl;

  EXPECT_EQ(f.Get(0, &ctrl), UVC_SUCCESS);
  EXPECT_EQ(ctrl.dwFrameInterval, k30Fps);
}

/* A zero dwFrameIntervalStep is a modulus of zero for any interval past the
 * shortest one. Take it to allow any interval in the range. */
TEST(StreamCtrlFormatSize, ZeroIntervalStepAllowsTheRange) {
  ContinuousFrameFixture f(0);
  uvc_stream_ctrl_t ctrl;

  EXPECT_EQ(f.Get(20, &ctrl), UVC_SUCCESS);
  EXPECT_EQ(ctrl.dwFrameInterval, 500000u);
  EXPECT_EQ(f.Get(5, &ctrl), UVC_ERROR_INVALID_MODE);
}

}  // namespace

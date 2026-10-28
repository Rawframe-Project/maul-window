// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Virtual input devices made through /dev/uinput, for the Linux gamepad
// tests: their events, their axes, and a thread that answers the force
// feedback the driver asks of a device, keeping what it was asked, or
// refusing every upload with an error of the test's choice.

#ifndef MAUL_WINDOW_TEST_LINUX_UINPUT_H
#define MAUL_WINDOW_TEST_LINUX_UINPUT_H

#include "test_harness.h"

#include <fcntl.h>
#include <linux/uinput.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

// A virtual device, and what its force feedback was asked.
typedef struct Device
{
    int fd;
    pthread_t thread;
    bool feedback;
    // What each upload answers: 0, or a negative errno that fails it.
    int32_t uploadResult;
    pthread_mutex_t lock;
    uint16_t strong;
    uint16_t weak;
    uint16_t length;
    int uploads;
    int plays;
    int stops;
} Device;

static inline uint64_t NowNs(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static inline void Emit(const Device* device, uint16_t type, uint16_t code, int32_t value)
{
    struct input_event event = {.type = type, .code = code, .value = value};
    CHECK(write(device->fd, &event, sizeof(event)) == (ssize_t)sizeof(event), "an event");
}

static inline void Report(const Device* device)
{
    Emit(device, EV_SYN, SYN_REPORT, 0);
}

static inline void AxisWith(int fd, uint16_t code, int32_t minimum, int32_t maximum,
                            int32_t resolution)
{
    struct uinput_abs_setup setup = {.code = code};
    setup.absinfo.minimum = minimum;
    setup.absinfo.maximum = maximum;
    setup.absinfo.resolution = resolution;
    (void)ioctl(fd, UI_SET_ABSBIT, code);
    (void)ioctl(fd, UI_ABS_SETUP, &setup);
}

static inline void Axis(int fd, uint16_t code, int32_t minimum, int32_t maximum)
{
    AxisWith(fd, code, minimum, maximum, 0);
}

// Answers the force feedback uploads the driver makes, which block until
// the device answers.
static inline void* Feedback(void* data)
{
    Device* device = data;
    struct input_event event;
    while (read(device->fd, &event, sizeof(event)) == (ssize_t)sizeof(event))
    {
        if (event.type == EV_UINPUT && event.code == UI_FF_UPLOAD)
        {
            struct uinput_ff_upload upload = {.request_id = (uint32_t)event.value};
            (void)ioctl(device->fd, UI_BEGIN_FF_UPLOAD, &upload);
            pthread_mutex_lock(&device->lock);
            device->uploads += 1;
            device->strong = upload.effect.u.rumble.strong_magnitude;
            device->weak = upload.effect.u.rumble.weak_magnitude;
            device->length = upload.effect.replay.length;
            upload.retval = device->uploadResult;
            pthread_mutex_unlock(&device->lock);
            (void)ioctl(device->fd, UI_END_FF_UPLOAD, &upload);
        }
        else if (event.type == EV_UINPUT && event.code == UI_FF_ERASE)
        {
            struct uinput_ff_erase erase = {.request_id = (uint32_t)event.value};
            (void)ioctl(device->fd, UI_BEGIN_FF_ERASE, &erase);
            (void)ioctl(device->fd, UI_END_FF_ERASE, &erase);
        }
        else if (event.type == EV_FF)
        {
            pthread_mutex_lock(&device->lock);
            device->plays += event.value > 0 ? 1 : 0;
            device->stops += event.value > 0 ? 0 : 1;
            pthread_mutex_unlock(&device->lock);
        }
    }
    return nullptr;
}

static inline void Destroy(Device* device)
{
    if (device->fd < 0)
    {
        return;
    }
    (void)ioctl(device->fd, UI_DEV_DESTROY);
    if (device->feedback)
    {
        (void)pthread_cancel(device->thread);
        (void)pthread_join(device->thread, nullptr);
    }
    close(device->fd);
    device->fd = -1;
}

#endif

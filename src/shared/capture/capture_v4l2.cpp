//========================================================================
//  This software is free: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License Version 3,
//  as published by the Free Software Foundation.
//
//  This software is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  Version 3 in the file COPYING that came with this distribution.
//  If not, see <http://www.gnu.org/licenses/>.
//========================================================================
/*!
  \file    capture_v4l2.cpp
  \brief   Image capture with the Video4Linux 2 API
  \author  Stefan Zickler, (C) 2009
  \author  Ben Johnson, (C) 2012
*/
//========================================================================

#include "capture_v4l2.h"
#include "conversions.h"
#include "timer.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#ifndef VDATA_NO_QT
CaptureV4L2::CaptureV4L2 ( VarList * _settings, QObject * parent ) : QObject ( parent ), CaptureInterface ( _settings )
#else
CaptureV4L2::CaptureV4L2 ( VarList * _settings ) : CaptureInterface ( _settings )
#endif
{
    fd = -1;

    settings->addChild ( v_colorout=new VarStringEnum ( "convert to mode",Colors::colorFormatToString ( COLOR_YUV422_UYVY ) ) );
    v_colorout->addItem ( Colors::colorFormatToString ( COLOR_RGB8 ) );
    v_colorout->addItem ( Colors::colorFormatToString ( COLOR_YUV422_UYVY ) );

    //FIXME - Identify device by device node name (/dev/video*)
    //FIXME - Identify device by connection
    //FIXME - Identify device by serial number
}

CaptureV4L2::~CaptureV4L2()
{
}

bool CaptureV4L2::startCapture()
{
#ifndef VDATA_NO_QT
    QMutexLocker lock(&mutex);
#endif

    //FIXME - Get device name from configuration
    const char *device ="/dev/video0";

    fd = open(device, O_RDWR);
    if (fd == -1)
    {
        fprintf(stderr, "CaptureV4L2::startCapture: Can't open %s: %m\n", device);
        return false;
    }

#if 0
    // Enumerate all formats
    //FIXME - Move this somewhere else so it happens whenever the device is chosen.
    //        Then a combobox can be populated with all available formats.
    struct v4l2_fmtdesc fmtdesc;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;
    while (true)
    {
        int ret = ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc);
        if (ret == -1)
        {
            if (errno == EINVAL)
            {
                // We've finished all formats
                break;
            } else {
                fprintf(stderr, "CaptureV4L2::startCapture: VIDIOC_ENUM_FMT returned %m\n");
                close(fd);
                fd = -1;
                return false;
            }
        }
        printf("Format %d: %s\n", fmtdesc.index, fmtdesc.description);
        ++fmtdesc.index;
    }
#endif

    // Set the video format.
    // This gives our fd exclusive access to the device.
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
    {
        fprintf(stderr, "CaptureV4L2::startCapture: VIDIOC_S_FMT failed: %m\n");
        close(fd);
        fd = -1;
        return false;
    }

    // Convert the format we actually got into a COLOR_* value
    enum ColorFormat color_format;
    switch (fmt.fmt.pix.pixelformat)
    {
        case V4L2_PIX_FMT_YUYV:
            color_format = COLOR_YUV422_YUYV;
            break;

        default:
        {
            char format_string[5];
            *(uint32_t *)format_string = fmt.fmt.pix.pixelformat;
            format_string[4] = 0;
            fprintf(stderr, "CaptureV4L2::startCapture: VIDIOC_S_FMT returned unsupported pixel format %s\n", format_string);
            close(fd);
            fd = -1;
            return false;
        }
    }

    // We only support mmap access.  read() is also possible with some drivers.

    // Allocate buffers
    struct v4l2_requestbuffers req;
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1)
    {
        fprintf(stderr, "CaptureV4L2::startCapture: VIDIOC_REQBUFS failed: %m\n");
        close(fd);
        fd = -1;
        return false;
    }

    // Map buffers into our address space
    buffers.resize(req.count);
    for (int i = 0; i < req.count; ++i)
    {
        // Get the size and mmap offset for this buffer
        struct v4l2_buffer buf;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1)
        {
            fprintf(stderr, "CaptureV4L2::startCapture: VIDIOC_QUERYBUF failed: %m\n");
            close(fd);
            fd = -1;
            return false;
        }

        // mmap this buffer
        void *data = mmap(0, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (data == 0)
        {
            fprintf(stderr, "CaptureV4L2::startCapture: mmap %d failed: %m\n", i);
            close(fd);
            fd = -1;
            return false;
        }

        // Set up the RawImage that getFrame will return later when this buffer is dequeued
        buffers[i].setWidth(fmt.fmt.pix.width);
        buffers[i].setHeight(fmt.fmt.pix.height);
        buffers[i].setColorFormat(color_format);
        buffers[i].setData((unsigned char *)data);

        // Enqueue this buffer
        if (ioctl(fd, VIDIOC_QBUF, &buf) == -1)
        {
            fprintf(stderr, "CaptureV4L2::startCapture: VIDIOC_QBUF failed: %m\n");
            close(fd);
            fd = -1;
            return false;
        }
    }
    
    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1)
    {
        fprintf(stderr, "CaptureV4L2::startCapture: VIDIOC_STREAMON failed: %m\n");
        close(fd);
        fd = -1;
        return false;
    }

    //FIXME - Make VarTypes read-only

    return true;
}

bool CaptureV4L2::stopCapture()
{
    cleanup();
    //FIXME - Make VarTypes read-write
    return true;
}

bool CaptureV4L2::isCapturing()
{
    return fd >= 0;
}

void CaptureV4L2::cleanup()
{
#ifndef VDATA_NO_QT
    QMutexLocker lock(&mutex);
#endif

    if (fd >= 0)
    {
        // Stop streaming
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1)
        {
            fprintf(stderr, "CaptureV4L2::cleanup: VIDIOC_STREAMOFF failed: %m\n");
        }

        // Close the device
        close(fd);
    }
    fd = -1;
}

RawImage CaptureV4L2::getFrame()
{
#ifndef VDATA_NO_QT
    QMutexLocker lock(&mutex);
#endif

    // Get a frame from the device
    last_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    last_buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_DQBUF, &last_buf) == -1)
    {
        fprintf(stderr, "CaptureV4L2::getFrame: VIDIOC_DQBUF failed: %m\n");
        return RawImage();
    }

    const struct timeval &tv = last_buf.timestamp;
    buffers[last_buf.index].setTime((double)tv.tv_sec + tv.tv_usec*(1.0E-6));

    return buffers[last_buf.index];
}

bool CaptureV4L2::copyAndConvertFrame ( const RawImage & src, RawImage & target )
{
#ifndef VDATA_NO_QT
    QMutexLocker lock(&mutex);
#endif

    ColorFormat output_fmt = Colors::stringToColorFormat ( v_colorout->getSelection().c_str() );
    ColorFormat src_fmt=src.getColorFormat();

    if ( target.getData() ==0 )
    {
        target.allocate ( output_fmt, src.getWidth(), src.getHeight() );
    } else {
        target.ensure_allocation ( output_fmt, src.getWidth(), src.getHeight() );
    }
    target.setTime ( src.getTime() );

    if (src_fmt == COLOR_YUV422_YUYV && output_fmt == COLOR_YUV422_UYVY)
    {
        //FIXME - Can you make this faster?
//         struct timespec t0;
//         clock_gettime(CLOCK_MONOTONIC, &t0);
        
#if 0
        int n = src.getWidth() * src.getHeight() / 2;
        const uint32_t *sd = (const uint32_t *)src.getData();
        uint32_t *td = (uint32_t *)target.getData();
        for (int i = 0; i < n; ++i)
        {
            uint32_t in = sd[i];
            td[i] = ((in & 0xff00ff00) >> 8) | ((in & 0x00ff00ff) << 8);
        }
#endif
#if 1
        int n = src.getWidth() * src.getHeight() * 2;
        const unsigned char *sd = src.getData();
        unsigned char *td = target.getData();
        for (int i = 0; i < n; i += 2)
        {
            td[i] = sd[i + 1];
            td[i + 1] = sd[i];
        }
#endif

//         struct timespec t1;
//         clock_gettime(CLOCK_MONOTONIC, &t1);
//         printf("%ld\n", (t1.tv_sec - t0.tv_sec) * 1000000000 + (t1.tv_nsec - t0.tv_nsec));
        
        target.setColorFormat(COLOR_YUV422_UYVY);
    } else {
        fprintf ( stderr,"Cannot copy and convert frame...unknown conversion selected from: %s to %s\n",
                    Colors::colorFormatToString ( src_fmt ).c_str(),
                    Colors::colorFormatToString ( output_fmt ).c_str() );
        return false;
    }

    return true;
}

void CaptureV4L2::releaseFrame()
{
#ifndef VDATA_NO_QT
    QMutexLocker lock(&mutex);
#endif

    if (ioctl(fd, VIDIOC_QBUF, &last_buf) == -1)
    {
        fprintf(stderr, "CaptureV4L2::releaseFrame: VIDIOC_QBUF failed: %m\n"); 
    }
}

string CaptureV4L2::getCaptureMethodName() const
{
    return "Video4Linux 2";
}

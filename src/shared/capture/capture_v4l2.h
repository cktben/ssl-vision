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
  \file    capture_generator.h
  \brief   Image capture with the Video4Linux 2 API
  \author  Stefan Zickler, (C) 2009
  \author  Ben Johnson, (C) 2012
*/
//========================================================================

#ifndef CAPTUREV4L2_H
#define CAPTUREV4L2_H

#include "captureinterface.h"
#include <string>
#include <vector>
#include <map>
#include "VarTypes.h"
#include <linux/videodev2.h>
#ifndef VDATA_NO_QT
  #include <QMutex>
  #include <QMutexLocker>
#else
  #include <pthread.h>
#endif


#ifndef VDATA_NO_QT
  #include <QMutex>
  //if using QT, inherit QObject as a base
class CaptureV4L2 : public QObject, public CaptureInterface
#else
class CaptureV4L2 : public CaptureInterface
#endif
{
#ifndef VDATA_NO_QT
  Q_OBJECT
/*   public slots: */
/*   void changed(VarType * group); */
  protected:
  QMutex mutex;
  public:
#endif

protected:
  int fd;

  // Adds choices to comboboxes and adds camera controls
  void populateConfiguration();

  struct v4l2_buffer last_buf;
  vector<RawImage> buffers;
  
  // Configuration
  VarStringEnum *v_colorout;
  VarString *v_device;
  VarList *v_controls;

  // Map from VarType to camera control ID for each control
  map<VarType *, uint32_t> camera_controls;

protected slots:
  void controlChanged(VarType *var);

public:
#ifndef VDATA_NO_QT
  CaptureV4L2(VarList * _settings, QObject * parent=0);
#else
  CaptureV4L2(VarList * _settings);
#endif
  ~CaptureV4L2();
    
  virtual bool startCapture();
  virtual bool stopCapture();
  virtual bool isCapturing();
  
  virtual RawImage getFrame();
  virtual void releaseFrame();
   
  void cleanup();

  virtual bool copyAndConvertFrame(const RawImage & src, RawImage & target);
  virtual string getCaptureMethodName() const;
};

#endif

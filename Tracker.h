#ifndef ARUCO_TRACKING_TRACKER_H
#define ARUCO_TRACKING_TRACKER_H

#include <opencv2/core.hpp>
#include "../camera-calibration/CVCalibration.h"

using namespace std;
using namespace cv;

class Tracker {
protected:
  Mat cameraMatrix, distCoeffs;
public:
  explicit Tracker(CVCalibration& cvl);
  virtual int getPose(Mat& frame, Vec3d& tVec, Vec3d& rVec) = 0;
  bool startStreamingTrack(int port = 0);
};

#endif //ARUCO_TRACKING_TRACKER_H

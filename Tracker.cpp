#include "Tracker.h"

#include <iostream>
#include <iomanip>

#include <opencv2/aruco.hpp>
#include <opencv2/calib3d.hpp>

using namespace std;
using namespace cv;

Tracker::Tracker(CVCalibration &cvl, bool _showFrame) {
  cameraMatrix = cvl.cameraMatrix;
  distCoeffs = cvl.distCoeffs;
  frameWidth = cvl.frameWidth;
  frameHeight = cvl.frameHeight;
  showFrame = _showFrame;
  fovx = 2 * atan(frameWidth / (2 * cameraMatrix.at<double>(0, 0))) * (180.0 / pi);
  fovy = 2 * atan(frameHeight / (2 * cameraMatrix.at<double>(1, 1))) * (180.0 / pi);
  cout << "FOVx: " << fovx << "\tFOVy: " << fovy << "\tFrame Width: "
       << frameWidth << "\tFrame Height: " << frameHeight << endl;
}

int Tracker::CLOCK() {
  struct timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<int>((t.tv_sec * 1000) + (t.tv_nsec * 1e-6));
}

double Tracker::avgDur(double newdur) {
  _avgdur = 0.98 * _avgdur + 0.02 * newdur;
  return _avgdur;
}

double Tracker::avgFPS() {
  if (CLOCK() - _fpsstart > 1000) {
    _fpsstart = CLOCK();
    _avgfps = 0.95 * _avgfps + 0.05 * _fps1sec;
    _fps1sec = 0;
  }
  
  _fps1sec++;
  return _avgfps;
}

// Checks if a matrix is a valid rotation matrix.
bool isRotationMatrix(Mat &R) {
  Mat Rt;
  transpose(R, Rt);
  Mat shouldBeIdentity = Rt * R;
  Mat I = Mat::eye(3, 3, shouldBeIdentity.type());
  return norm(I, shouldBeIdentity) < 1e-6;
}

Vec3d rotationMatrixToEulerAngles(Mat &R) {
  assert(isRotationMatrix(R));
  double sy = sqrt(R.at<double>(0, 0) * R.at<double>(0, 0) + R.at<double>(1, 0) * R.at<double>(1, 0));
  bool singular = sy < 1e-6; // If
  double x, y, z;
  if (!singular) {
    x = atan2(R.at<double>(2, 1), R.at<double>(2, 2));
    y = atan2(-R.at<double>(2, 0), sy);
    z = atan2(R.at<double>(1, 0), R.at<double>(0, 0));
  } else {
    x = atan2(-R.at<double>(1, 2), R.at<double>(1, 1));
    y = atan2(-R.at<double>(2, 0), sy);
    z = 0;
  }
  return Vec3d(x, y, z);
}

void Tracker::loopedTracking(VideoCapture vid, bool saveVideo, string filename) {
  Mat frame;
  VideoWriter rawVideo, procVideo;
  string rawFilename = filename + " (Raw).avi";
  string procFilename = filename + " (Proc).avi";
  Vec3d rVec, tVec;
  int datano = 0;
  
  cout << "Row\tFrame\tRunning\tTimestamp\tFPS\tDist1\tDist2\tDist3\n";
  if (showFrame) namedWindow("Camera Feed", WINDOW_AUTOSIZE);
  if (saveVideo) {
    rawVideo = VideoWriter(rawFilename, VideoWriter::fourcc('M', 'J', 'P', 'G'), 30,
                           Size(frameWidth, frameHeight),
                           true);
    procVideo = VideoWriter(procFilename, VideoWriter::fourcc('M', 'J', 'P', 'G'), 30,
                            Size(frameWidth, frameHeight),
                            true);
  }
  while (true) {
    if (!vid.read(frame)) {
      cerr << "Unable to read next frame. Ending tracking.\n";
      break;
    };
    if (saveVideo) rawVideo.write(frame);
    
    auto start = static_cast<clock_t>(CLOCK());
    
    if (detectLandingPad(frame) && getPose(frame, tVec, rVec) > 0) {
      
      datano++;
      double dur = CLOCK() - start;
      auto t = time(nullptr);
      auto tm = *localtime(&t);
      cout << datano << "\t"
           << vid.get(CAP_PROP_POS_FRAMES) << "\t"
           << put_time(&tm, "%H:%M:%S") << "\t"
           << avgDur(dur) << "\t"
           << avgFPS() << "\t"
           << tVec[0] << "\t"
           << tVec[1] << "\t"
           << tVec[2] << "\t"
           << endl;
    }
    if (showFrame) imshow("Camera Feed", frame);
    if (saveVideo) procVideo.write(frame);
    char character = static_cast<char>(waitKey(50));
    switch (character) {
      case ' ': // Space key -> save image
        waitKey(0);
      case 27: // Esc key -> exit prog.
        break;
    }
  }
  
  // When everything done, release the video capture and write object
  vid.release();
  
  if (saveVideo) {
    rawVideo.release();
    procVideo.release();
  }
  
  // Closes all the windows
  if (showFrame) destroyAllWindows();
}

void Tracker::getGlobalPose(const Vec3d &rVec, const Vec3d &tVec, Vec3d &ctVec) const {
  Mat R_flip = Mat::zeros(3, 3, CV_64F);
  R_flip.at<double>(0, 0) = 1.0;
  R_flip.at<double>(1, 1) = -1.0;
  R_flip.at<double>(2, 2) = -1.0;
  
  Mat R_ct = Mat::eye(3, 3, CV_64F);
  Rodrigues(rVec, R_ct);
  Mat R_tc = R_ct.t();
  Mat rMat = R_flip * R_tc;
  Vec3d euler = rotationMatrixToEulerAngles(rMat);
  Matx<double, 1, 3> tVect = tVec.t();
  Mat tVecC = -1 * (R_tc * tVec).t();
  
  ctVec[0] = -1 * (tVecC.at<double>(0, 0)); // TODO: Abstract this into the board logic
  ctVec[1] = tVecC.at<double>(0, 1);
  ctVec[2] = tVecC.at<double>(0, 2);
}

bool Tracker::startStreamingTrack(int port, bool saveVideo, string filename) {
  VideoCapture vid(port);
  vid.set(CAP_PROP_FRAME_WIDTH, frameWidth);
  vid.set(CAP_PROP_FRAME_HEIGHT, frameHeight);
  if (!vid.isOpened()) {
    cerr << "Unable to read video stream. Is the camera mount path correct?\n";
    return false;
  }
  loopedTracking(vid, saveVideo, filename);
  return true;
}

bool Tracker::startVideoTrack(const string &fname, bool saveVideo, string filename) {
  VideoCapture vid(fname);
  if (!vid.isOpened()) {
    cerr << "Unable to read video file. Is the filepath correct?\n";
    return false;
  }
  loopedTracking(vid, saveVideo, filename);
  return true;
}

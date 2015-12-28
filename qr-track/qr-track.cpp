
#include <iostream>
#include <stdlib.h>
#include <string>
using namespace std;

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/imgproc/imgproc.hpp"
using namespace cv;

#include <zbar.h>
using namespace zbar;



/*
  ItemData
  Structure aggregating geometric and ID data about each detected item in the image
    X: horizontal coordinate of the item
    Y: vertical coordinate of the item
    theta: orientation of the item
    ID: number identifying the item, as a positive integer
*/
struct ItemData {
  int X;
  int Y;
  float theta;
  unsigned int ID;
} ;



/*
  readProj
  Function importing reprojection data as a transformation matrix from a YML file
    filename: input
      Full path and name to the YML file to read
    M: output
      OpenCV matrix to return transformation matrix
*/
static bool readProj( const char* filename, Mat& M)
{
  FileStorage fs(filename, FileStorage::READ);
  if( !fs.isOpened() )
    return false;
  
  FileNode Mn = fs["transform_mat"];
  if ( Mn.empty() )
    return false;

  // Get the transformation matrix
  Mn >> M;
  return true;
}



/*
  readScene
  Function importing scene reference data from a YML file
    filename: input
      Full path and name to the YML file to read
    w: output
      Width of the scene
    h: output
      Height of the scene
*/
static bool readScene( const char* filename, int& w, int& h)
{
  FileStorage fs(filename, FileStorage::READ);
  if ( !fs.isOpened() )
    return false;
  
  FileNode wn = fs["Width"], hn = fs["Height"];
  if ( wn.empty() || hn.empty() )
    return false;

  // Get the scene's dimensions
  wn >> w;
  hn >> h;
  return true;
}



/*
  openCam
  Function attempting to connect to a camera up to index 9
    videocap: output
      VideoCapture object corresponding to the first found camera
    index: input output
      As input: first camera index to try
      As output: last camera index tried, index of the first found camera if applicable
*/
static bool openCam(VideoCapture& videocap, int& index)
{
  bool opened = false;
    int maxindex = index + 10;

  // Try to open a camera among the ten first found
    while (!opened && (index < maxindex)) {
    videocap = VideoCapture(index);
    opened = videocap.isOpened();
    index++;
  }

  index--; // Get the last index actually used

  return opened;
}



/*
  openAVI
  Function attempting to open an AVI video file
    videocap: output
      VideoCapture object corresponding to the opened file
    path: input
      Full path and name to the AVI file to open
*/
static bool openAVI(VideoCapture& videocap, char* path)
{
  videocap = VideoCapture(path);
  return videocap.isOpened();
}



/*
  process
  Function scanning an image taken from a calibrated camera to identify QR or bar codes
    imsname: input
      Full path and name to the image to read
      The image should be taken with a fixed camera already calibrated
    projname: input
      Full path and name to the YML file from which to get reprojection data
      About required YML structure, refer to example file
    scnname: input
      Full path and name to the YML file from which to get scene reference data
      About required YML structure, refer to example file
    source: input
      String indicating which source will be used : AVI file or camera
      source should be a full path to an AVI file
      or an integer corresponding to the index of the first camera to try to connect to
*/
int process(const char* projname, const char* scnname, char* source)
{
  // # Configuration phase #

  // Load transformation matrix and scene data from reference files
  Mat M;
  int width, height;
  bool proj_loaded = readProj(projname, M);
  cout << ( proj_loaded ? "Reprojection data successfully loaded from: " : "Failed to load reprojection data from: ") << projname << endl;
  bool scn_loaded = readScene(scnname, width, height);
  cout << ( scn_loaded ? "Scene data successfully loaded from: " : "Failed to load scene data from: ") << scnname << endl;

  // Open the video source
  VideoCapture videocap;
  bool cap_opened = false;
  string src(source);

  if(src.substr(src.find_last_of(".") + 1) == "avi") {
    cout << "Source detected: AVI video file." << endl;
    cap_opened = openAVI(videocap, source);
    cout << ( cap_opened ? "Video successfully opened at: " : "Failed to open video file at: ") << src << endl;
  }
  else {
    cout << "Source detected: camera." << endl;
    int camindex = atoi(source);
    if (camindex < 0) {
      cerr << "Camera index given is invalid: " << source << ". Positive integer expected." << endl;
      exit(EXIT_FAILURE);
    }
    cap_opened = openCam(videocap, camindex);
    cout << ( cap_opened ? "Camera connection successfully opened at index " : "Failed to connect to camera! Final index: ") << camindex << endl;
  }

  // In case of error during configuration phase, the program exits
  if (! (cap_opened && proj_loaded && scn_loaded) ) {
    cerr << "Aborting scanning..." << endl;
    exit(EXIT_FAILURE);
  }
  else {
    Mat frame, gray; // Images that will be read and scanned
    bool frame_OK = false;

    ImageScanner scanner; // Code scanner
    scanner.set_config(ZBAR_NONE, ZBAR_CFG_ENABLE, 1);

    //* # SHOW # Display current frame in a window
    namedWindow("Reprojected frame", 1);
    // # SHOW # */

    //* # HIGHLIGHT # Delimit detected symbols in the reprojected image
    Scalar color(0, 0, 255); // BGR pure red to highlight detected symbols
    namedWindow("Found symbols", 1);
    // # HIGHLIGHT # */

    while(true) {
      frame_OK = videocap.read(frame);
      waitKey(1); // Allows the buffer to refresh
      if (! (frame.data && frame_OK)) {
        cerr << "Failed to load image from source!" << endl;
        exit(EXIT_FAILURE);
      }

      warpPerspective(frame, frame, M, Size(width, height)); // Apply this transformation on the whole image
      cvtColor(frame, gray, CV_BGR2GRAY); // Get grayscale image for scanning phase

      //* # SHOW #
      imshow("Reprojected frame", frame);
      // # SHOW # */
      
      uchar *raw = (uchar*) gray.data; // Raw image data
      Image image(width, height, "Y800", raw, width * height);
      
      // Scan for codes in the image
      int nsyms = scanner.scan(image);

      // Extract results

      //* # DATA # Write symbols' data in the console
      cout << nsyms << " symbol(s) found in the given image" << endl;
      // # DATA # */

      for(Image::SymbolIterator symbol = image.symbol_begin(); symbol != image.symbol_end(); ++symbol) {
        vector<Point> vp; // Build a vector of points delimiting the symbol
        int n = symbol->get_location_size();
        for(int i = 0; i < n; i++) {
          vp.push_back(Point(symbol->get_location_x(i),symbol->get_location_y(i)));
        }

        RotatedRect rect = minAreaRect(vp); // Find the smallest rectangle containing the symbol
        Point2f center = rect.center;
        float angle = rect.angle;
        
        //* # DATA #
        cout << "Data: \"" << symbol->get_data() << "\" - Angle: " << angle << " - Center: " << center << endl;
        // # DATA # */

        //* # HIGHLIGHT #
        Point2f pts[4]; // Draw the rectangle's edges on the reprojected image
        rect.points(pts);
        for(int i = 0; i < 4; i++)
          line(frame, pts[i], pts[(i + 1) % 4], color, 2);
        // # HIGHLIGHT # */
      }

      //* # HIGHLIGHT #
      imshow("Found symbols", frame);
      waitKey(30);
      // # HIGHLIGHT # */
    }
    return EXIT_SUCCESS;
  }
}



#define param 3

int main(int args, char* argv[])
{
  if (args != param + 1) {
    if (args < param + 1) {
      cout << "Too few arguments!";
    }
    else {
      cout << "Too many arguments!";
    }
    cerr << " Number given: " << args - 1 << endl << "Usage: qr-track <calib-data.yml> <scn-data.yml> <video-source>" << endl;
    exit(EXIT_FAILURE);
  }
  else {
    cout << "# ---------------------" << endl << "QR tracker based on reprojection data" << endl << "# ---------------------" << endl << endl;
    return process(argv[1], argv[2], argv[3]);
  }
}
#include "WebcamHandler.h"

WebcamHandler::WebcamHandler()
{
	m_edgeLength = 64;
	m_frameWidth = 1280;
	m_frameHeight = 720;
	m_threads = 8;
	m_faceAreaOffset = 4;
	m_relativeEyePositionX = 0.27;
	m_relativeEyePositionY = 0.27;


	m_loop = true;
	m_action = true;

	m_detectionColorMin = new Scalar(48, 10, 60);
	m_detectionColorMax = new Scalar(78, 255, 255);

	int faHeight = max(m_edgeLength, m_frameHeight / 4);
	int faWidth = faHeight;
	int faX = (m_frameWidth - faWidth) / 2;
	int faY = (m_frameHeight - faHeight) / 2;

	m_faceArea = new Rect(faX, faY, faWidth, faHeight);
	
	m_rbm1000 = initializeRBM("weights\\WildFaces_64x64_rgb_1kh_58380it.out", "weights\\WildFaces_64x64_rgb_1kh_58380it.bin", m_threads);
	m_rbm1500 = initializeRBM("weights\\WildFaces_64x64_rgb_1,5kh_104000it.out", "weights\\WildFaces_64x64_rgb_1,5kh_104000it.bin", m_threads);
	m_rbm2000 = initializeRBM("weights\\WildFaces_64x64_rgb_2kh_10440it.out", "weights\\WildFaces_64x64_rgb_2kh_10440it.bin", m_threads);
}


WebcamHandler::~WebcamHandler()
{
	delete m_faceArea;
	delete m_detectionColorMin;
	delete m_detectionColorMax;
	delete m_rbm1000;
	delete m_rbm1500;
	delete m_rbm2000;
}

void WebcamHandler::run()
{

	// initialize webcam
	VideoCapture cap = VideoCapture(0);
	cap.set(CV_CAP_PROP_FRAME_WIDTH, m_frameWidth);
	cap.set(CV_CAP_PROP_FRAME_HEIGHT, m_frameHeight);	

	// initialize window
	namedWindow("Settings", CV_WINDOW_AUTOSIZE);
	namedWindow("FaceRepair", CV_WINDOW_NORMAL);
	cvSetWindowProperty("FaceRepair", CV_WND_PROP_FULLSCREEN, CV_WINDOW_FULLSCREEN);

	cvWaitKey(1000);

	float* hidden;
	float* visible;

	while (m_loop)
	{
		// read frame and continue with next frame if not successfull
		Mat frame;
		cap.retrieve(frame);
		flip(frame, frame, 1);

		// take subimage at faceArea
		Mat subimage;
		frame(*m_faceArea).copyTo(subimage);
		Mat subimageHSV;
		cvtColor(subimage, subimageHSV, COLOR_BGR2HSV); //Convert the captured frame from BGR to HSV

		// detect color
		Mat mask;
		inRange(subimageHSV, *m_detectionColorMin, *m_detectionColorMax, mask);
		erode(mask, mask, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
		dilate(mask, mask, getStructuringElement(MORPH_ELLIPSE, Size(15, 15)));
		Mat invertedMask = 255 - mask;

		// scale to rbm input size
		Size size = Size(m_edgeLength, m_edgeLength);
		Mat scaledSubimage;	
		resize(subimage, scaledSubimage, size, 0.0, 0.0, INTER_LINEAR);
		Mat scaledMask;
		resize(mask, scaledMask, size, 0.0, 0.0, INTER_NEAREST);
		Mat invertedScaledMask = 255 - scaledMask;

		// calc mean rgb of preserved area
		Scalar bgr = mean(scaledSubimage, invertedScaledMask);

		// set mean rgb at reconstructionArea
		scaledSubimage.setTo(bgr, scaledMask);

		// subimage to normalized float array
		visible = matToNormalizedFloatArrayWithBias(&scaledSubimage);

		// process RBMs
		hidden = m_rbm1000->runHidden(visible, 1);
		delete visible;
		hidden[0] = 1;
		visible = m_rbm1000->runVisible(hidden, 1);
		delete hidden;
		visible[0] = 1;
		resetPreservedArea(&scaledSubimage, &invertedScaledMask, visible);

		hidden = m_rbm1500->runHidden(visible, 1);
		delete visible;
		hidden[0] = 1;
		visible = m_rbm1500->runVisible(hidden, 1);
		delete hidden;
		visible[0] = 1;
		resetPreservedArea(&scaledSubimage, &invertedScaledMask, visible);

		hidden = m_rbm2000->runHidden(visible, 1);
		delete visible;
		hidden[0] = 1;
		visible = m_rbm2000->runVisible(hidden, 1);
		delete hidden;

		// normalized float array to subimage
		normalizedFloatArrayToMatWithoutBias(visible, &scaledSubimage);

		// scale to original faceArea size
		Mat result;
		size = Size(m_faceArea->width, m_faceArea->height);
		resize(scaledSubimage, result, size, 0.0, 0.0, INTER_CUBIC);

		// reset pixels of preserved area in native resolution
		subimage.copyTo(result, invertedMask);

		// create fullscreen image
		Mat fs;
		frame.copyTo(fs);
		result.copyTo(fs(*m_faceArea));
		flip(fs, fs, 1);
		
		// maybe not necessary
		//result.copyTo(frame(*m_faceArea));
		
		// paint visualizations for settings image
		rectangle(frame, *m_faceArea, Scalar(0, 255, 0));
		Point* eyePositions = calculateEyePositions(m_faceArea, m_relativeEyePositionX, m_relativeEyePositionY);
		circle(frame, eyePositions[0], 4, Scalar(255, 255, 0));
		circle(frame, eyePositions[1], 4, Scalar(255, 255, 0));
		delete eyePositions;

		// show frames
		imshow("Settings", frame);
		imshow("FaceRepair", fs);
		
		// check keyboard input
		checkKeys();
	}
	// terminate webcam
	cap.release();
}

void WebcamHandler::checkKeys()
{
	int key = cvWaitKey(30);
	if (key == -1) return;
	
	cout << key << endl;

	switch (key)
	{
		case 27: // escape
			m_loop = false; break; // terminate programm
		case 13: // enter
			break;
		case 105: // i
			break;
		case 2424832: // left arrow
			moveLeftFacePosition(); break;
		case 2490368: // up arrow
			moveUpFacePosition(); break;
		case 2555904: // right arrow
			moveRightFacePosition(); break;
		case 2621440: // down arrow
			moveDownFacePosition(); break;
		case 110: // n
			decreaseFaceSize(); break;
		case 109: // m
			increaseFaceSize(); break;
	}
}

void WebcamHandler::moveLeftFacePosition()
{
	m_faceArea->x = max(m_faceArea->x - m_faceAreaOffset, 0);
}

void WebcamHandler::moveUpFacePosition()
{
	m_faceArea->y = max(m_faceArea->y - m_faceAreaOffset, 0);
}

void WebcamHandler::moveRightFacePosition()
{
	m_faceArea->x = min(m_faceArea->x + m_faceAreaOffset, m_frameWidth - m_faceArea->width);
}

void WebcamHandler::moveDownFacePosition()
{
	m_faceArea->y = min(m_faceArea->y + m_faceAreaOffset, m_frameHeight - m_faceArea->height);
}

void WebcamHandler::decreaseFaceSize()
{
	m_faceArea->width = max(m_edgeLength, m_faceArea->width - m_faceAreaOffset);
	m_faceArea->height = m_faceArea->width;
}

void WebcamHandler::increaseFaceSize()
{
	int edge = m_faceArea->width + m_faceAreaOffset;
	if (m_faceArea->x + edge <= m_frameWidth && m_faceArea->y + edge <= m_frameHeight)
	{
		m_faceArea->width = edge;
		m_faceArea->height = edge;
	}
}

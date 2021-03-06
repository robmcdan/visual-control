#include "digitalreceptionist_gui.h"
#include "webcamwidget_gui.h"
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <QWidget>
//#include <qt4>
#include <qt5/QtGui/QCloseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>


#include "../cognition/detector/facedetector.h"
#include "../cognition/trainerimage.h"
#include "../cognition/recognizer/fisherfacerecognizer.h"
#include "digitalreceptionist.h"

namespace gui
{

	const cv::Size DigitalReceptionist::testImageSize(150,200);

	DigitalReceptionist::DigitalReceptionist(QWidget *parent, Qt::WindowFlags flags)
		: QMainWindow(parent, flags)
	{
		setupFramework();
		setupGUI();

		setMinimumSize(QSize(640,480));

		frameCaptureThread = boost::thread(
			boost::bind(&cognition::FrameCapture::startCapturing, frameCapture));

		faceDetectorThread = boost::thread(
			boost::bind(&cognition::Detector::threadStart, faceDetector));

		Logger::getInstance().log("Ready...");
	}

	DigitalReceptionist::~DigitalReceptionist()
	{
	}

	void DigitalReceptionist::setupFramework()
	{
		using boost::shared_ptr;
		using cognition::DetailedFaceDetector;
		using cognition::FisherFaceRecognizer;

		//videoCapture = shared_ptr<VideoCapture>( new VideoCapture(0) );
		frameCapture = shared_ptr<cognition::FrameCapture>( new cognition::FrameCapture(32) );

		//update the path of the classifier to the local path of your openCV installation!
		faceDetector = shared_ptr<DetailedFaceDetector>( 
			new DetailedFaceDetector(DetailedFaceDetector::ALL, 
			"/home/rob/git/opencv/data/haarcascades/haarcascade_frontalface_alt.xml",
			frameCapture.get(), false, 1.16));

		faceDetector->loadCascade(DetailedFaceDetector::EYES, "/home/rob/git/opencv/data/haarcascades/haarcascade_eye.xml");
		faceDetector->loadCascade(DetailedFaceDetector::NOSE, "/home/rob/git/opencv/data/haarcascades/haarcascade_mcs_nose.xml");
		faceDetector->loadCascade(DetailedFaceDetector::MOUTH, "/home/rob/git/opencv/data/haarcascades/haarcascade_mcs_mouth.xml");

		frameCapture->addFrameReceiver(faceDetector.get());

		recognizer = shared_ptr<FisherFaceRecognizer>( new cognition::FisherFaceRecognizer );
	}

	void DigitalReceptionist::closeEvent(QCloseEvent *event)
	{
		//webcamController->unregister();
		faceDetector->removeController(webcamWidget);

		//qDebug() << "controller count = " << faceDetector->getControllerCount();

		frameCapture->stopCapturing();

		faceDetector->requestTreadStop();

		frameCaptureThread.interrupt();

		faceDetectorThread.interrupt();

	//	qDebug() << "Close event called, joining";
		frameCaptureThread.join();

		faceDetectorThread.join();

		//event->accept();
	
		//QMainWindow::closeEvent(event);
	}

	void DigitalReceptionist::setupGUI()
	{
		webcamWidget = new WebcamWidget(this, frameCapture.get());

		faceDetector->addController(webcamWidget); 

		logWidget = new QListWidget;

		captureTrainingImageButton = new QPushButton(tr("Capture face and store it as training image"));
		connect(captureTrainingImageButton, SIGNAL(clicked()), this, SLOT(captureTrainingImage()));

		trainRecognizerButton = new QPushButton(tr("Train recognizer"));
		connect(trainRecognizerButton, SIGNAL(clicked()), this, SLOT(trainRecognizer()));

		recognizeFaceButton = new QPushButton(tr("Recognize Visible Faces"));
		connect(recognizeFaceButton, SIGNAL(clicked()), this, SLOT(recognizeFaces()));

		QWidget *centralWidget = new QWidget;
		QVBoxLayout *mainLayout = new QVBoxLayout;
		QHBoxLayout *buttonsLayout = new QHBoxLayout;

		buttonsLayout->addWidget(captureTrainingImageButton);
		buttonsLayout->addWidget(trainRecognizerButton);
		buttonsLayout->addWidget(recognizeFaceButton);
	
		mainLayout->addWidget(webcamWidget);
		mainLayout->addLayout(buttonsLayout);
		mainLayout->addWidget(logWidget);

		centralWidget->setLayout(mainLayout);

		Logger::getInstance().setLogWidget(logWidget);
		
		setCentralWidget(centralWidget);
	}

	void DigitalReceptionist::captureTrainingImage()
	{
		cv::Mat frame;
		cognition::Detector::RectVector faces;

		captureFrameAndFaces(faces, frame);
				
		if(faces.size() == 0)
		{
			QMessageBox::information(this, "No faces found", "The detector did not find any faces!");
		} 
		else 
		{
			frame = frame.clone();
			cognition::TrainerImage convertor(testImageSize, true, "/home/rob/git/digital-receptionist/src/images/");
			cv::Mat faceRegion;
			for(std::vector<cv::Rect>::iterator face = faces.begin(); face != faces.end(); ++face)
			{
				faceRegion = frame(*face);
				QString filename = QInputDialog::getText(this,
					tr("Image name"),
					tr("enter image name (enter extension too, it determines the image format!)"));

				if(filename.size() < 1) continue;

				if(!convertor.processAndSaveImage(faceRegion, filename.toStdString()))
					QMessageBox::information(this, "Failed", "Could not process and save the image!");
			}
		}
	}

	void DigitalReceptionist::trainRecognizer()
	{
		using namespace boost::filesystem;

		path dir("/home/rob/git/digital-receptionist/src/images/"); //todo add configuration member for directory
		directory_iterator end;

		for(directory_iterator file(dir); file != end; ++file)
		{
			if(is_regular_file(*file)){
				path fn = file->path().filename();
				recognizer->addTrainingImage(file->path().string(), fn.string());
			}
		}

		if(recognizer->train())
			QMessageBox::information(this, "Success", "The recognizer has succesfully finished training!");
		else
			QMessageBox::information(this, "Error", "The recognizer has indicated that it did not train correctly!");
	}

	void DigitalReceptionist::recognizeFaces()
	{
		if(recognizer->trained())
		{
			cv::Mat frame;
			cognition::Detector::RectVector faces;

			captureFrameAndFaces(faces, frame);
				
			if(faces.size() == 0)
				QMessageBox::information(this, "No faces found", "The detector did not find any faces!");
			else 
			{
				frame = frame.clone();
				cognition::TrainerImage convertor(testImageSize, true);
				cv::Mat faceRegion;
				cv::Mat processedFace;

				for(std::vector<cv::Rect>::iterator face = faces.begin(); face != faces.end(); ++face)
				{
					faceRegion = frame(*face);
					processedFace = convertor.processImage(faceRegion);
					std::string name = recognizer->recognize(processedFace);
					Logger::getInstance().log(std::string("Recognized: ") + name);
				}
			}
		}
		else 
		{
			QMessageBox::information(this, "Recognizer is not trained", "Recognizer is not trained or failed to train, add enough training images and train the recognizer!");
		}
	}

	void DigitalReceptionist::captureFrameAndFaces(cognition::Detector::RectVector &rects, cv::Mat &frame)
	{
		rects = webcamWidget->getCurrentFaces();
		frame = webcamWidget->getCurrentFrame();
		//cognition::FaceDetector detector("/home/rob/git/opencv/data/haarcascades/haarcascade_frontalface_alt.xml");
		//*frameCapture->getCaptureDevice() >> frame;

		//detector.receiveFrame(frame);
		//detector.processFrame();
		//rects = detector.getAreas();
	}
}

#include "moc_digitalreceptionist_gui.cpp"
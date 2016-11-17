/*===================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center,
Division of Medical and Biological Informatics.
All rights reserved.

This software is distributed WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.

See LICENSE.txt or http://www.mitk.org for details.

===================================================================*/

// std dependencies
#include <ctime>
#include <chrono>

// mitk dependencies
#include "mitkUSDiPhASDevice.h"
#include "mitkUSDiPhASImageSource.h"
#include <mitkIOUtil.h>
#include "mitkUSDiPhASBModeImageFilter.h"
#include "mitkImageCast.h"
#include "mitkITKImageImport.h"

// itk dependencies
#include "itkImage.h"
#include "itkResampleImageFilter.h"
#include "itkCastImageFilter.h"
#include "itkCropImageFilter.h"
#include "itkRescaleIntensityImageFilter.h"
#include "itkIntensityWindowingImageFilter.h"
#include <itkIndex.h>


mitk::USDiPhASImageSource::USDiPhASImageSource(mitk::USDiPhASDevice* device)
  : m_Image(mitk::Image::New()),
  m_device(device),
  startTime(((float)std::clock()) / CLOCKS_PER_SEC),
  useGUIOutPut(false),
  m_DataType(DataType::Image_uChar),
  m_GUIOutput(nullptr),
  useBModeFilter(false),
  currentlyRecording(false),
  m_DataTypeModified(true),
  m_DataTypeNext(DataType::Image_uChar),
  m_UseBModeFilterModified(false),
  m_UseBModeFilterNext(false),
  m_CurrentImageTimestamp(0),
  m_PyroConnected(false),
  m_ImageTimestampBuffer()
{
  m_ImageMutex = itk::FastMutexLock::New();

  m_BufferSize = 100;
  m_ImageTimestampBuffer.insert(m_ImageTimestampBuffer.begin(), m_BufferSize, 0);
  m_LastWrittenImage = m_BufferSize - 1;
  m_ImageBuffer.insert(m_ImageBuffer.begin(), m_BufferSize, nullptr);
}

mitk::USDiPhASImageSource::~USDiPhASImageSource()
{
  // close the pyro
  MITK_INFO("Pyro Debug") << "StopDataAcquisition: " << m_Pyro->StopDataAcquisition();
  MITK_INFO("Pyro Debug") << "CloseConnection: " << m_Pyro->CloseConnection();
  m_PyroConnected = false;
  m_Pyro = nullptr;
}

void mitk::USDiPhASImageSource::GetNextRawImage( mitk::Image::Pointer& image)
{
  // we get this image pointer from the USDevice and write into it the data we got from the DiPhAS API
  if (m_DataTypeModified)
  {
    SetDataType(m_DataTypeNext);
    m_DataTypeModified = false;
  }
  if (m_UseBModeFilterModified)
  {
    SetUseBModeFilter(m_UseBModeFilterNext);
    m_UseBModeFilterModified = false;
  }

  float cf = 0;
  int i = 100;
  while ((cf <= 0)&&(i>90))
  {
    if (m_ImageTimestampBuffer[(m_LastWrittenImage + i) % 100] != 0)
    {
      cf = m_Pyro->GetClosestEnergyInmJ(m_ImageTimestampBuffer[(m_LastWrittenImage + i) % 100]);
      i--;
      //MITK_INFO << cf;
    }
    else
    {
      cf = 40; // todo: correct by fixed value and say that you dont compensate
      i = 99;
    }
  }
  i++;

  image = &(*m_ImageBuffer[(m_LastWrittenImage + i) % 100]);

  if (image != nullptr)
  {
    // do image processing before displaying it
    if (useBModeFilter && m_DataType == DataType::Beamformed_Short)
    {
      // apply BmodeFilter to the image
      image = ApplyBmodeFilter(image);
    }
  }
}

mitk::Image::Pointer mitk::USDiPhASImageSource::ApplyBmodeFilter2d(mitk::Image::Pointer inputImage)
{
  // we use this seperate ApplyBmodeFilter Method for processing of images that are to be saved later on (see SetRecordingStatus(bool)).

  // the image needs to be of floating point type for the envelope filter to work; the casting is done automatically by the CastToItkImage
  typedef itk::Image< float, 2 > itkInputImageType;
  typedef itk::Image< short, 2 > itkOutputImageType;
  typedef itk::PhotoacousticBModeImageFilter < itkInputImageType, itkOutputImageType > PhotoacousticBModeImageFilter;

  PhotoacousticBModeImageFilter::Pointer photoacousticBModeFilter = PhotoacousticBModeImageFilter::New();
  itkInputImageType::Pointer itkImage;

  mitk::CastToItkImage(inputImage, itkImage);
  photoacousticBModeFilter->SetInput(itkImage);
  photoacousticBModeFilter->SetDirection(1);
  return mitk::GrabItkImageMemory(photoacousticBModeFilter->GetOutput());
}

mitk::Image::Pointer mitk::USDiPhASImageSource::ApplyBmodeFilter(mitk::Image::Pointer inputImage)
{
  //MITK_INFO << "Applying BMode Filter";
  // the image needs to be of floating point type for the envelope filter to work; the casting is done automatically by the CastToItkImage
  typedef itk::Image< float, 3 > itkFloatImageType;
  typedef itk::PhotoacousticBModeImageFilter < itkFloatImageType, itkFloatImageType > PhotoacousticBModeImageFilter;
  PhotoacousticBModeImageFilter::Pointer photoacousticBModeFilter = PhotoacousticBModeImageFilter::New();
  typedef itk::ResampleImageFilter < itkFloatImageType, itkFloatImageType > ResampleImageFilter;
  ResampleImageFilter::Pointer resampleImageFilter = ResampleImageFilter::New();
  itkFloatImageType::Pointer itkImage;
  mitk::CastToItkImage(inputImage, itkImage);
  photoacousticBModeFilter->SetInput(itkImage);
  photoacousticBModeFilter->SetDirection(1);
  itkFloatImageType::Pointer bmode = photoacousticBModeFilter->GetOutput();
  itkFloatImageType::SpacingType outputSpacing;
  itkFloatImageType::SizeType inputSize = itkImage->GetLargestPossibleRegion().GetSize();
  itkFloatImageType::SizeType outputSize = inputSize;
  outputSize[0] = 256;

  outputSpacing[0] = itkImage->GetSpacing()[0] * (static_cast<double>(inputSize[0]) / static_cast<double>(outputSize[0]));
  outputSpacing[1] = outputSpacing[0] / 2;
  outputSpacing[2] = 0.6;

  outputSize[1] = inputSize[1] * itkImage->GetSpacing()[1] / outputSpacing[1];

  typedef itk::IdentityTransform<double, 3> TransformType;
  resampleImageFilter->SetInput(bmode);
  resampleImageFilter->SetSize(outputSize);
  resampleImageFilter->SetOutputSpacing(outputSpacing);
  resampleImageFilter->SetTransform(TransformType::New());

  resampleImageFilter->UpdateLargestPossibleRegion();
  return mitk::GrabItkImageMemory(resampleImageFilter->GetOutput());
}

void mitk::USDiPhASImageSource::ImageDataCallback(
    short* rfDataChannelData,
    int& channelDataChannelsPerDataset,
    int& channelDataSamplesPerChannel,
    int& channelDataTotalDatasets,

    short* rfDataArrayBeamformed,
    int& beamformedLines,
    int& beamformedSamples,
    int& beamformedTotalDatasets,

    unsigned char* imageData,
    int& imageWidth,
    int& imageHeight,
    int& imageBytesPerPixel,
    int& imageSetsTotal,

    double& timeStamp)
{
  if (!m_PyroConnected)
  {
    m_Pyro = mitk::OphirPyro::New();
    MITK_INFO << "[Pyro Debug] OpenConnection: " << m_Pyro->OpenConnection();
    MITK_INFO << "[Pyro Debug] StartDataAcquisition: " << m_Pyro->StartDataAcquisition();
    m_PyroConnected = true;
  }

  bool writeImage = ((m_DataType == DataType::Image_uChar) && (imageData != nullptr)) || ((m_DataType == DataType::Beamformed_Short) && (rfDataArrayBeamformed != nullptr)) && !m_Image.IsNull();
  if (writeImage)
  {
    //get the timestamp we might save later on
    m_CurrentImageTimestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    m_ImageTimestampRecord.push_back(m_CurrentImageTimestamp);

    // create a new image and initialize it
    mitk::Image::Pointer image = mitk::Image::New();

    switch (m_DataType)
    {
      case DataType::Image_uChar: {
        m_ImageDimensions[0] = imageWidth;
        m_ImageDimensions[1] = imageHeight;
        m_ImageDimensions[2] = imageSetsTotal;
        image->Initialize(mitk::MakeScalarPixelType<unsigned char>(), 3, m_ImageDimensions);
        break;
      }
      case DataType::Beamformed_Short: {
        m_ImageDimensions[0] = beamformedLines;
        m_ImageDimensions[1] = beamformedSamples;
        m_ImageDimensions[2] = beamformedTotalDatasets;
        image->Initialize(mitk::MakeScalarPixelType<short>(), 3, m_ImageDimensions);
        break;
      }
    }
    image->GetGeometry()->SetSpacing(m_ImageSpacing);
    image->GetGeometry()->Modified();

    // write the given buffer into the image
    switch (m_DataType)
    {
      case DataType::Image_uChar: {
        for (int i = 0; i < imageSetsTotal; i++) {
          image->SetSlice(&imageData[i*imageHeight*imageWidth], i);
        }
        break;
      }

      case DataType::Beamformed_Short: {
        short* flipme = new short[beamformedLines*beamformedSamples*beamformedTotalDatasets];
        int pixelsPerImage = beamformedLines*beamformedSamples;

        for (int currentSet = 0; currentSet < beamformedTotalDatasets; currentSet++)
        {
            for (int sample = 0; sample < beamformedSamples; sample++)
            {
              for (int line = 0; line < beamformedLines; line++)
              {
                flipme[sample*beamformedLines + line + pixelsPerImage*currentSet]
                  = rfDataArrayBeamformed[line*beamformedSamples + sample + pixelsPerImage*currentSet];
              }
            } // the beamformed pa image is flipped by 90 degrees; we need to flip it manually
        }
        
        for (int i = 0; i < beamformedTotalDatasets; i++) {
          image->SetSlice(&flipme[i*beamformedLines*beamformedSamples], i);
          // set every image to a different slice
        }

        delete[] flipme;
        break;
      }
    }

    itk::Index<3> pixel = { { (image->GetDimension(0) / 2), 84, 0 } }; //22/532*2048
    if (!m_Pyro->IsSyncDelaySet() &&(image->GetPixelValueByIndex(pixel) < -30)) // #MagicNumber
    {
      MITK_INFO << "Setting SyncDelay";
      m_Pyro->SetSyncDelay(m_CurrentImageTimestamp);
    }

    // if the user decides to start recording, we feed the vector the generated images
    if (currentlyRecording) {
      for (int index = 0; index < image->GetDimension(2); ++index)
      {
        if (image->IsSliceSet(index))
        {
          m_recordedImages.push_back(Image::New());
          m_recordedImages.back()->Initialize(image->GetPixelType(), 2, image->GetDimensions());
          m_recordedImages.back()->SetGeometry(image->GetGeometry());

          mitk::ImageReadAccessor inputReadAccessor(image, image->GetSliceData(index));
          m_recordedImages.back()->SetSlice(inputReadAccessor.GetData());
        }
      }
      // save timestamps for each laser image!
    }
    // [sic!] Thomas: This kills everything, so we commented it out and now it doesnt kill anything..
  //  if (!useGUIOutPut && m_GUIOutput) {
  //    // Need to do this because the program initializes the GUI twice
  //    // this is probably a bug in UltrasoundSupport, if it's fixed the timing becomes unneccesary
  //    float timePassed = ((float)std::clock()) / CLOCKS_PER_SEC - startTime;
  //    if (timePassed > 10)
  //    {
  //      useGUIOutPut = true;
  //    }
  //  }
  //  if (useGUIOutPut) {
  //    // pass some beamformer state infos to the GUI
  //    getSystemInfo(&BeamformerInfos);

  //    std::ostringstream s;
  //    s << "state info: PRF:" << BeamformerInfos.systemPRF << "Hz, datarate: " << BeamformerInfos.dataTransferRateMBit << "MBit/s";

  //    m_GUIOutput(QString::fromStdString(s.str()));
  //  }
    m_ImageTimestampBuffer[(m_LastWrittenImage + 1) % m_BufferSize] = m_CurrentImageTimestamp;
    m_ImageBuffer[(m_LastWrittenImage + 1) % m_BufferSize] = image;
    m_LastWrittenImage = (m_LastWrittenImage + 1) % m_BufferSize;
  }
}

void mitk::USDiPhASImageSource::UpdateImageGeometry()
{
  MITK_INFO << "Retreaving Image Geometry Information for Spacing...";
  float& recordTime        = m_device->GetScanMode().receivePhaseLengthSeconds;
  int& speedOfSound        = m_device->GetScanMode().averageSpeedOfSound;
  float& pitch             = m_device->GetScanMode().reconstructedLinePitchMmOrAngleDegree;
  int& reconstructionLines = m_device->GetScanMode().reconstructionLines;

  switch (m_DataType)
  {
    case DataType::Image_uChar : {
      int& imageWidth = m_device->GetScanMode().imageWidth;
      int& imageHeight = m_device->GetScanMode().imageHeight;
      m_ImageSpacing[0] = pitch * reconstructionLines / imageWidth;
      m_ImageSpacing[1] = recordTime * speedOfSound / 2 * 1000 / imageHeight;
      break;
    }
    case DataType::Beamformed_Short : {
      int& imageWidth = reconstructionLines;
      int& imageHeight = m_device->GetScanMode().reconstructionSamplesPerLine;
      m_ImageSpacing[0] = pitch;
      m_ImageSpacing[1] = recordTime * speedOfSound / 2 * 1000 / imageHeight;
      break;
    }
  }
  m_ImageSpacing[2] = 0.6;

  MITK_INFO << "Retreaving Image Geometry Information for Spacing " << m_ImageSpacing[0] << " ... " << m_ImageSpacing[1] << " ... " << m_ImageSpacing[2] << " ...[DONE]";
}

void mitk::USDiPhASImageSource::ModifyDataType(DataType DataT)
{
  m_DataTypeModified = true;
  m_DataTypeNext = DataT;
}

void mitk::USDiPhASImageSource::ModifyUseBModeFilter(bool isSet)
{
  m_UseBModeFilterModified = true;
  m_UseBModeFilterNext = isSet;
}

void mitk::USDiPhASImageSource::SetDataType(DataType DataT)
{
  if (DataT != m_DataType)
  {
    m_DataType = DataT;
    MITK_INFO << "Setting new DataType..." << DataT;
    switch (m_DataType)
    {
    case DataType::Image_uChar :
      MITK_INFO << "height: " << m_device->GetScanMode().imageHeight << " width: " << m_device->GetScanMode().imageWidth;
      break;
    case DataType::Beamformed_Short :
      MITK_INFO << "samples: " << m_device->GetScanMode().reconstructionSamplesPerLine << " lines: " << m_device->GetScanMode().reconstructionLines;
      break;
    }
  }
}

void mitk::USDiPhASImageSource::SetGUIOutput(std::function<void(QString)> out)
{
  USDiPhASImageSource::m_GUIOutput = out;
  startTime = ((float)std::clock()) / CLOCKS_PER_SEC; //wait till the callback is available again
  useGUIOutPut = false;
}

void mitk::USDiPhASImageSource::SetUseBModeFilter(bool isSet)
{
  useBModeFilter = isSet;
}

// this is just a little function to set the filenames below right
inline void replaceAll(std::string& str, const std::string& from, const std::string& to) {
  if (from.empty())
    return;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
  }
}

void mitk::USDiPhASImageSource::SetRecordingStatus(bool record)
{
  // start the recording process
  if (record)
  {
    m_recordedImages.clear();  // we make sure there are no leftovers
    m_ImageTimestampRecord.clear();      // also for the timestamps
    m_pixelValues.clear();     // aaaand for the pixel values

    // tell the callback to start recording images
    currentlyRecording = true;
  }
  // save images, end recording, and clean up
  else
  {
    currentlyRecording = false;

    // get the time and date, put them into a nice string and create a folder for the images
    time_t time = std::time(nullptr);
    time_t* timeptr = &time;
    std::string currentDate = std::ctime(timeptr);
    replaceAll(currentDate, ":", "-");
    currentDate.pop_back();
    std::string MakeFolder = "mkdir \"c:/DiPhASImageData/" + currentDate + "\"";
    system(MakeFolder.c_str());

    // initialize file paths and the images
    Image::Pointer PAImage = Image::New();
    Image::Pointer USImage = Image::New();
    std::string pathPA = "c:\\DiPhASImageData\\" + currentDate + "\\" + "PAImages" + ".nrrd";
    std::string pathUS = "c:\\DiPhASImageData\\" + currentDate + "\\" + "USImages" + ".nrrd";
    std::string pathTS = "c:\\DiPhASImageData\\" + currentDate + "\\" + "TimestampsImages" + ".csv";

    // order the images and save them
    if (m_device->GetScanMode().beamformingAlgorithm == (int)Beamforming::Interleaved_OA_US) // save a PAImage if we used interleaved mode
    {
      bool saveImageData = false;
      if (saveImageData)
      {
        OrderImagesInterleaved(PAImage, USImage);
        mitk::IOUtil::Save(USImage, pathUS);
        mitk::IOUtil::Save(PAImage, pathPA);
      }

      // read the pixelvalues of the enveloped images at this position
      itk::Index<3> pixel = { { m_recordedImages.at(1)->GetDimension(0) / 2, 84, 0 } }; //22/532*2048
      GetPixelValues(pixel);

      // save the timestamps!
      ofstream timestampFile;

      timestampFile.open(pathTS);
      timestampFile << ",timestamp,pixelvalue"; // write the header

      for (int index = 0; index < m_ImageTimestampRecord.size(); ++index)
      {
        timestampFile << "\n" << index << "," << m_ImageTimestampRecord.at(index) << "," << m_pixelValues.at(index);
      }
      timestampFile.close();
    }
    else if (m_device->GetScanMode().beamformingAlgorithm == (int)Beamforming::PlaneWaveCompound) // save no PAImage if we used US only mode
    {
      OrderImagesUltrasound(USImage);
      mitk::IOUtil::Save(USImage, pathUS);
    }

    m_pixelValues.clear();    // clean up the pixel values
    m_recordedImages.clear(); // clean up the images
    m_ImageTimestampRecord.clear();     // clean up the timestamps
  }
}

void mitk::USDiPhASImageSource::GetPixelValues(itk::Index<3> pixel)
{
  unsigned int events = m_device->GetScanMode().transmitEventsCount + 1; // the PA event is not included in the transmitEvents, so we add 1 here
  for (int index = 0; index < m_recordedImages.size(); index += events)  // omit sound images
  {
    m_recordedImages.at(index) = ApplyBmodeFilter2d(m_recordedImages.at(index));
    m_pixelValues.push_back(m_recordedImages.at(index).GetPointer()->GetPixelValueByIndex(pixel));
  }
}

void mitk::USDiPhASImageSource::OrderImagesInterleaved(Image::Pointer PAImage, Image::Pointer USImage)
{
  unsigned int width  = 32;
  unsigned int height = 32;
  unsigned int events = m_device->GetScanMode().transmitEventsCount + 1; // the PA event is not included in the transmitEvents, so we add 1 here

  if (m_DataType == DataType::Beamformed_Short)
  {
    width = m_device->GetScanMode().reconstructionLines;
    height = m_device->GetScanMode().reconstructionSamplesPerLine;
  }
  else if (m_DataType == DataType::Image_uChar)
  {
    width = m_device->GetScanMode().imageWidth;
    height = m_device->GetScanMode().imageHeight;
  }

  unsigned int dimLaser[] = { width, height, m_recordedImages.size() / events};
  unsigned int dimSound[] = { width, height, m_recordedImages.size() / events * (events-1)};

  PAImage->Initialize(m_recordedImages.back()->GetPixelType(), 3, dimLaser);
  PAImage->SetGeometry(m_recordedImages.back()->GetGeometry());
  USImage->Initialize(m_recordedImages.back()->GetPixelType(), 3, dimSound);
  USImage->SetGeometry(m_recordedImages.back()->GetGeometry());

  for (int index = 0; index < m_recordedImages.size(); ++index)
  {
    mitk::ImageReadAccessor inputReadAccessor(m_recordedImages.at(index));
    if (index % events == 0)
    {
      PAImage->SetSlice(inputReadAccessor.GetData(), index / events);
    }
    else
    {
      USImage->SetSlice(inputReadAccessor.GetData(), ((index - (index % events)) / events) + (index % events)-1);
    }
  }
}

void mitk::USDiPhASImageSource::OrderImagesUltrasound(Image::Pointer SoundImage)
{
  unsigned int width  = 32;
  unsigned int height = 32;
  unsigned int events = m_device->GetScanMode().transmitEventsCount;

  if (m_DataType == DataType::Beamformed_Short)
  {
    width = m_device->GetScanMode().reconstructionLines;
    height = m_device->GetScanMode().reconstructionSamplesPerLine;
  }
  else if (m_DataType == DataType::Image_uChar)
  {
    width = m_device->GetScanMode().imageWidth;
    height = m_device->GetScanMode().imageHeight;
  }

  unsigned int dimSound[] = { width, height, m_recordedImages.size()};

  SoundImage->Initialize(m_recordedImages.back()->GetPixelType(), 3, dimSound);
  SoundImage->SetGeometry(m_recordedImages.back()->GetGeometry());

  for (int index = 0; index < m_recordedImages.size(); ++index)
  {
    mitk::ImageReadAccessor inputReadAccessor(m_recordedImages.at(index));
    SoundImage->SetSlice(inputReadAccessor.GetData(), index);
  }
}
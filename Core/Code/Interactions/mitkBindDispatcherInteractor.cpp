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

#include "mitkBindDispatcherInteractor.h"
#include "mitkMessage.h"

mitk::BindDispatcherInteractor::BindDispatcherInteractor()
{
  m_Dispatcher = mitk::Dispatcher::New();
}

void mitk::BindDispatcherInteractor::SetDataStorage(mitk::DataStorage::Pointer dataStorage)
{
  // Set/Change Datastorage. This registers BDI to listen for events of DataStorage, to be informed when
  // a DataNode with a Interactor is added/modified/removed.

  // clean up events from previous datastorage
  if (m_DataStorage.IsNotNull())
  {
    UnRegisterDataStorageEvents();
  }
  m_DataStorage = dataStorage;
  if (m_DataStorage.IsNotNull())
  {
    RegisterDataStorageEvents();
  }
}


mitk::BindDispatcherInteractor::~BindDispatcherInteractor()
{
}

void mitk::BindDispatcherInteractor::RegisterInteractor(const mitk::DataNode* dataNode)
{
  if (m_Dispatcher.IsNotNull())
  {
    m_Dispatcher->AddDataInteractor(dataNode);
  }

}

void mitk::BindDispatcherInteractor::RegisterDataStorageEvents()
{
  m_DataStorage->AddNodeEvent.AddListener(
      MessageDelegate1<BindDispatcherInteractor, const mitk::DataNode*>(this, &BindDispatcherInteractor::RegisterInteractor));
  m_DataStorage->RemoveNodeEvent.AddListener(
      MessageDelegate1<BindDispatcherInteractor, const mitk::DataNode*>(this, &BindDispatcherInteractor::UnRegisterInteractor));
  m_DataStorage->ChangedNodeEvent.AddListener(
      MessageDelegate1<BindDispatcherInteractor, const mitk::DataNode*>(this, &BindDispatcherInteractor::RegisterInteractor));
}

void mitk::BindDispatcherInteractor::UnRegisterInteractor(const DataNode* dataNode)
{
  if (m_Dispatcher.IsNotNull())
   {
     m_Dispatcher->RemoveDataInteractor(dataNode);
   }
}

mitk::Dispatcher::Pointer mitk::BindDispatcherInteractor::GetDispatcher()
{
  return m_Dispatcher;
}

void mitk::BindDispatcherInteractor::SetDispatcher(Dispatcher::Pointer dispatcher)
{
  m_Dispatcher = dispatcher;
}



void mitk::BindDispatcherInteractor::UnRegisterDataStorageEvents()
{
  m_DataStorage->AddNodeEvent.RemoveListener(
      MessageDelegate1<BindDispatcherInteractor, const mitk::DataNode*>(this, &BindDispatcherInteractor::RegisterInteractor));
  m_DataStorage->RemoveNodeEvent.RemoveListener(
      MessageDelegate1<BindDispatcherInteractor, const mitk::DataNode*>(this, &BindDispatcherInteractor::UnRegisterInteractor));
  m_DataStorage->ChangedNodeEvent.RemoveListener(
      MessageDelegate1<BindDispatcherInteractor, const mitk::DataNode*>(this, &BindDispatcherInteractor::RegisterInteractor));
}

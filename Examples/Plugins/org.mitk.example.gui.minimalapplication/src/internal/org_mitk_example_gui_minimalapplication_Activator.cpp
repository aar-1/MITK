/*============================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center (DKFZ)
All rights reserved.

Use of this source code is governed by a 3-clause BSD license that can be
found in the LICENSE file.

============================================================================*/

#include "org_mitk_example_gui_minimalapplication_Activator.h"

#include "MinimalApplication.h"
#include "MinimalPerspective.h"

void org_mitk_example_gui_minimalapplication_Activator::start(ctkPluginContext *context)
{
  BERRY_REGISTER_EXTENSION_CLASS(MinimalApplication, context)
  BERRY_REGISTER_EXTENSION_CLASS(MinimalPerspective, context)
}

void org_mitk_example_gui_minimalapplication_Activator::stop(ctkPluginContext *context)
{
  Q_UNUSED(context)
}

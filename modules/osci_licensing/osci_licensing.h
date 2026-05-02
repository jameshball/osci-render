#pragma once

/*******************************************************************************
 The block below describes the properties of this module, and is read by
 the Projucer to automatically generate project code that uses it.

 BEGIN_JUCE_MODULE_DECLARATION

  ID:                osci_licensing
  vendor:            jameshball
  version:           0.1.0
  name:              osci-render licensing
  description:       Licensing and update helpers for osci-render products
  website:           https://osci-render.com
  license:           GPLv3
  minimumCppStandard: 20

  dependencies:      juce_core, juce_data_structures, juce_events, juce_cryptography

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <juce_cryptography/juce_cryptography.h>

#include "license/osci_LicenseToken.h"
#include "network/osci_BackendClient.h"
#include "license/osci_LicenseManager.h"
#include "system/osci_HardwareInfo.h"
#include "update/osci_UpdateChecker.h"
#include "update/osci_Downloader.h"
#include "update/osci_InstallerLauncher.h"

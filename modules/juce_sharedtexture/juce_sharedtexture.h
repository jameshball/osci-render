/*
  ==============================================================================

  ==============================================================================
*/


/*******************************************************************************
 The block below describes the properties of this module, and is read by
 the Projucer to automatically generate project code that uses it.
 For details about the syntax and how to create or use a module, see the
 JUCE Module Format.txt file.


 BEGIN_JUCE_MODULE_DECLARATION

  ID:               juce_sharedtexture
  vendor:           benkuper
  version:          1.0.0
  name:             Shared Texture
  description:      A Spout/Syphon plugin to allow manipulation of Shared Textures amongst softwares
  website:          https://github.com/benkuper/juce_sharedtexture
  license:          GPLv3

  dependencies:     juce_core, juce_opengl
  windowsLibs :		SpoutLibrary

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/


#ifndef JUCE_SHAREDTEXTURE_UI_H_INCLUDED
#define JUCE_SHAREDTEXTURE_UI_H_INCLUDED

//==============================================================================

/** Config: SHAREDTEXTURE_USE_CPU_IMAGE
	If set to 1, this will use a CPU image to store the texture, and will be slower but will work on all platforms and for dashboard.
*/
#ifndef SHAREDTEXTURE_USE_CPU_IMAGE
#define SHAREDTEXTURE_USE_CPU_IMAGE 1
#endif


#ifdef _MSC_VER
 #pragma warning (push)
 // Disable warnings for long class names, padding, and undefined preprocessor definitions.
 #pragma warning (disable: 4251 4786 4668 4820)
#endif




#include "juce_core/juce_core.h"
#include "juce_gui_basics/juce_gui_basics.h"
#include "juce_opengl/juce_opengl.h"

#if JUCE_WINDOWS
#include "SpoutLibrary.h"
#elif JUCE_MAC
#endif


#include "SharedTexture.h"

#endif   // JUCE_SHAREDTEXTURE_UI_H_INCLUDED

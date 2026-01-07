// $Id$
// $HeadURL$

/*
 ==============================================================================
 
 This file is part of the UGEN++ library
 Copyright 2008-11 The University of the West of England.
 by Martin Robinson
 
 ------------------------------------------------------------------------------
 
 UGEN++ can be redistributed and/or modified under the terms of the
 GNU General Public License, as published by the Free Software Foundation;
 either version 2 of the License, or (at your option) any later version.
 
 UGEN++ is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with UGEN++; if not, visit www.gnu.org/licenses or write to the
 Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 Boston, MA 02111-1307 USA
 
 The idea for this project and code in the UGen implementations is
 derived from SuperCollider which is also released under the 
 GNU General Public License:
 
 SuperCollider real time audio synthesis system
 Copyright (c) 2002 James McCartney. All rights reserved.
 http://www.audiosynth.com
 
 ==============================================================================
 */

#ifndef _UGEN_ugen_JuceUtility_H_
#define _UGEN_ugen_JuceUtility_H_

#include <JuceHeader.h>

/** A little Juce class used as a pop-up editor. */
class PopupComponent :	public juce::Component,
						public juce::Timer
{
private:
	int counter;
	const int maxCount; 	
	static int activePopups;
	
public:
	PopupComponent(const int max = 20);	
	~PopupComponent();
	void paint(juce::Graphics &g);	
	void mouseDown(juce::MouseEvent const& e);
    
    virtual void expired() = 0;
	
	static int getActivePopups() { return activePopups; }

protected:
	void resetCounter();	
	void timerCallback();
	void expire();
};

#endif // _UGEN_ugen_JuceUtility_H_

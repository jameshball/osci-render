#pragma once

#ifdef __OBJC__
@class SyphonOpenGLClient;
@class SyphonOpenGLServer;
#else
class SyphonOpenGLClient;
class SyphonOpenGLServer;
#endif

class SharedTextureSender
{
public:
	SharedTextureSender(const juce::String &name, int width, int height, bool enabled = true);
	~SharedTextureSender();

#if JUCE_WINDOWS
	SPOUTLIBRARY * sender;
#elif JUCE_MAC
    SyphonOpenGLServer* sender;
#endif

	bool isInit;

	juce::String sharingName;
	bool sharingNameChanged;
	bool enabled;


	juce::Image image;
	juce::OpenGLFrameBuffer *fbo;
	juce::OpenGLFrameBuffer* externalFBO;
    GLuint textureId;

	int width;
	int height;

	bool canDraw();

	void setSize(int w, int h);

	void setExternalFBO(juce::OpenGLFrameBuffer * newFBO);

	void initGL();
	void renderGL();
	void clearGL();

	void setSharingName(juce::String value);
	void setEnabled(bool value);

	void createImageDefinition();
	void setupNativeSender(bool forceRecreation = false);

	class SharedTextureListener
	{
	public:
		virtual ~SharedTextureListener() {}
		virtual void drawSharedTexture(juce::Graphics &g, juce::Rectangle<int> r) = 0;
	};

	juce::ListenerList<SharedTextureListener> sharedTextureListeners;
	void addSharedTextureListener(SharedTextureListener* newListener) { sharedTextureListeners.add(newListener); }
	void removeSharedTextureListener(SharedTextureListener* listener) { sharedTextureListeners.remove(listener); }
};


#if JUCE_WINDOWS
class SpoutReceiver;
#endif

class SharedTextureReceiver
{
public:
	SharedTextureReceiver(const juce::String &sharingName = juce::String(), const juce::String &sharingAppName = juce::String());
	~SharedTextureReceiver();

#if JUCE_WINDOWS
	SPOUTLIBRARY * receiver;
#elif JUCE_MAC
    SyphonOpenGLClient* receiver;
#endif

	bool enabled;
	juce::String sharingName;
    juce::String sharingAppName; //for syphon
	bool isInit;
	bool isConnected;

	int width;
	int height;
	bool invertImage;

	juce::Image image;
	juce::OpenGLFrameBuffer * fbo;

	bool useCPUImage; //useful for manipulations like getPixelAt, but not optimized
	juce::Image outImage;

	void setSharingName(const juce::String& name, const juce::String& appName = "");

	void setConnected(bool value);

	void setUseCPUImage(bool value);
	juce::Image & getImage();
	
	bool canDraw();
	void createReceiver();
	void createImageDefinition();

	void initGL();
	void renderGL();
	void clearGL();


	class  Listener
	{
	public:
		virtual ~Listener() {}
		virtual void textureUpdated(SharedTextureReceiver *) {}
		virtual void connectionChanged(SharedTextureReceiver *) {}
	};

	juce::ListenerList<Listener> listeners;
	void addListener(Listener* newListener) { listeners.add(newListener); }
	void removeListener(Listener* listener) { listeners.remove(listener); }

};

class SharedTextureManager
{
public:
	juce_DeclareSingleton(SharedTextureManager,true);

	SharedTextureManager();
	virtual ~SharedTextureManager();

	juce::OwnedArray<SharedTextureSender> senders;
	juce::OwnedArray<SharedTextureReceiver> receivers;

	juce::Array<SharedTextureSender *> sendersToRemove;
	juce::Array<SharedTextureReceiver *> receiversToRemove;

	SharedTextureSender * addSender(const juce::String &name, int width, int height, bool enabled = true);
	SharedTextureReceiver * addReceiver(const juce::String &name = juce::String(), const juce::String &appName = juce::String());
    
    

	void removeSender(SharedTextureSender * sender, bool force = false);
	void removeReceiver(SharedTextureReceiver * receiver, bool force = false);

	virtual void initGL();
	virtual void renderGL();
	virtual void clearGL();
    
    juce::StringArray getAvailableSenders();

	class  Listener
	{
	public:
		virtual ~Listener() {}
		virtual void receiverRemoved(SharedTextureReceiver *) {}
		virtual void senderRemoved(SharedTextureSender *) {}
		virtual void GLInitialized() {}
	};

	juce::ListenerList<Listener> listeners;
	void addListener(Listener* newListener) { listeners.add(newListener); }
	void removeListener(Listener* listener) { listeners.remove(listener); }

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SharedTextureManager)


};

// THIS CODE HAS BEEN HEAVILY ADAPTED FROM https://dood.al/oscilloscope/ AS PERMITTED BY THE AUTHOR

import * as Juce from "./index.js";

var AudioSystem =
{
	microphoneActive : false,

    init : function (bufferSize)
    {
        this.bufferSize = bufferSize;
        this.timePerSample = 1/externalSampleRate;
        this.oldXSamples = new Float32Array(this.bufferSize);
		this.oldYSamples = new Float32Array(this.bufferSize);
		this.oldZSamples = new Float32Array(this.bufferSize);
    	this.smoothedXSamples = new Float32Array(Filter.nSmoothedSamples);
		this.smoothedYSamples = new Float32Array(Filter.nSmoothedSamples);
		this.smoothedZSamples = new Float32Array(Filter.nSmoothedSamples);
    },

    startSound : function()
    {
        window.__JUCE__.backend.addEventListener("audioUpdated", doScriptProcessor);
    }
}

var Filter =
{
	lanczosTweak : 1.5,

	init : function(bufferSize, a, steps)
	{
		this.bufferSize = bufferSize;
    	this.a = a;
    	this.steps = steps;
    	this.radius = a * steps;
    	this.nSmoothedSamples = this.bufferSize*this.steps + 1;
    	this.allSamples = new Float32Array(2*this.bufferSize);

    	this.createLanczosKernel();
    },


	generateSmoothedSamples : function (oldSamples, samples, smoothedSamples)
	{
		var bufferSize = this.bufferSize;
		var allSamples = this.allSamples;
		var nSmoothedSamples = this.nSmoothedSamples;
		var a = this.a;
		var steps = this.steps;
		var K = this.K;

		for (var i=0; i<bufferSize; i++)
		{
			allSamples[i] = oldSamples[i];
			allSamples[bufferSize+i] = samples[i];
		}

		var pStart = bufferSize - 2*a;
		var pEnd = pStart + bufferSize;
		var i = 0;
		for (var position=pStart; position<pEnd; position++)
		{
			smoothedSamples[i] = allSamples[position];
			i += 1;
			for (var r=1; r<steps; r++)
			{
				var smoothedSample = 0;
				for (var s= -a+1; s<a; s++)
				{
					var sample = allSamples[position+s];
					var kernelPosition = -r+s*steps;
					if (kernelPosition<0) smoothedSample += sample * K[-kernelPosition];
					else smoothedSample += sample * K[kernelPosition];
				}
				smoothedSamples[i] = smoothedSample;
				i += 1;
			}
		}

		smoothedSamples[nSmoothedSamples-1] = allSamples[2*bufferSize-2*a];
	},

    createLanczosKernel : function ()
    {
    	this.K = new Float32Array(this.radius);
    	this.K[0] = 1;
    	for (var i =1; i<this.radius; i++)
    	{
    		var piX = (Math.PI * i) / this.steps;
    		var sinc = Math.sin(piX)/piX;
    		var window = this.a * Math.sin(piX/this.a) / piX;
    		this.K[i] = sinc*Math.pow(window, this.lanczosTweak);
    	}
    }
}

var Render =
{
	init : function()
	{
		this.canvas = document.getElementById("crtCanvas");
		this.onResize();
		window.onresize = this.onResize;
		window.gl = this.canvas.getContext("webgl", {preserveDrawingBuffer: true},  { alpha: false } );
		gl.viewport(0, 0, this.canvas.width, this.canvas.height);
		gl.enable(gl.BLEND);
		gl.blendEquation( gl.FUNC_ADD );
		gl.clearColor(0.0, 0.0, 0.0, 1.0);
		gl.clear(gl.COLOR_BUFFER_BIT);
		gl.colorMask(true, true, true, true);
		var ext1 = gl.getExtension('OES_texture_float');
		var ext2 = gl.getExtension('OES_texture_float_linear');
		//this.ext = gl.getExtension('OES_texture_half_float');
		//this.ext2 = gl.getExtension('OES_texture_half_float_linear');
		this.fadeAmount = 0.2*AudioSystem.bufferSize/512;


		this.fullScreenQuad = new Float32Array([
			-1, 1, 1, 1,  1,-1,  // Triangle 1
			-1, 1, 1,-1, -1,-1   // Triangle 2
		  	]);

		this.simpleShader = this.createShader("vertex","fragment");
		this.simpleShader.vertexPosition = gl.getAttribLocation(this.simpleShader, "vertexPosition");
		this.simpleShader.colour = gl.getUniformLocation(this.simpleShader, "colour");

		this.lineShader = this.createShader("gaussianVertex","gaussianFragment");
		this.lineShader.aStart = gl.getAttribLocation(this.lineShader, "aStart");
		this.lineShader.aEnd = gl.getAttribLocation(this.lineShader, "aEnd");
		this.lineShader.aIdx = gl.getAttribLocation(this.lineShader, "aIdx");
		this.lineShader.uGain = gl.getUniformLocation(this.lineShader, "uGain");
		this.lineShader.uSize = gl.getUniformLocation(this.lineShader, "uSize");
		this.lineShader.uInvert = gl.getUniformLocation(this.lineShader, "uInvert");
		this.lineShader.uIntensity = gl.getUniformLocation(this.lineShader, "uIntensity");
		this.lineShader.uNEdges = gl.getUniformLocation(this.lineShader, "uNEdges");
		this.lineShader.uFadeAmount = gl.getUniformLocation(this.lineShader, "uFadeAmount");
		this.lineShader.uScreen = gl.getUniformLocation(this.lineShader, "uScreen");

		this.outputShader = this.createShader("outputVertex","outputFragment");
		this.outputShader.aPos = gl.getAttribLocation(this.outputShader, "aPos");
		this.outputShader.uTexture0 = gl.getUniformLocation(this.outputShader, "uTexture0");
		this.outputShader.uTexture1 = gl.getUniformLocation(this.outputShader, "uTexture1");
		this.outputShader.uTexture2 = gl.getUniformLocation(this.outputShader, "uTexture2");
		this.outputShader.uTexture3 = gl.getUniformLocation(this.outputShader, "uTexture3");
		this.outputShader.uExposure = gl.getUniformLocation(this.outputShader, "uExposure");
        this.outputShader.uSaturation = gl.getUniformLocation(this.outputShader, "uSaturation");
		this.outputShader.uColour = gl.getUniformLocation(this.outputShader, "uColour");
		this.outputShader.uResizeForCanvas = gl.getUniformLocation(this.outputShader, "uResizeForCanvas");

		this.texturedShader = this.createShader("texturedVertexWithResize","texturedFragment");
		this.texturedShader.aPos = gl.getAttribLocation(this.texturedShader, "aPos");
		this.texturedShader.uTexture0 = gl.getUniformLocation(this.texturedShader, "uTexture0");
		this.texturedShader.uResizeForCanvas = gl.getUniformLocation(this.texturedShader, "uResizeForCanvas");

		this.blurShader = this.createShader("texturedVertex","blurFragment");
		this.blurShader.aPos = gl.getAttribLocation(this.blurShader, "aPos");
		this.blurShader.uTexture0 = gl.getUniformLocation(this.blurShader, "uTexture0");
		this.blurShader.uOffset = gl.getUniformLocation(this.blurShader, "uOffset");

		this.vertexBuffer = gl.createBuffer();
		this.setupTextures();
	},

	setupArrays : function(nPoints)
	{
		this.nPoints = nPoints;
		this.nEdges = this.nPoints-1;

		this.quadIndexBuffer = gl.createBuffer();
		var indices = new Float32Array(4*this.nEdges);
		for (var i=0; i<indices.length; i++)
		{
			indices[i] = i;
		}
		gl.bindBuffer(gl.ARRAY_BUFFER, this.quadIndexBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, indices, gl.STATIC_DRAW);
		gl.bindBuffer(gl.ARRAY_BUFFER, null);

		this.vertexIndexBuffer = gl.createBuffer();
		var len = this.nEdges * 2 * 3,
		indices = new Uint16Array(len);
		for (var i = 0, pos = 0; i < len;)
		{
			indices[i++] = pos;
			indices[i++] = pos + 2;
			indices[i++] = pos + 1;
			indices[i++] = pos + 1;
			indices[i++] = pos + 2;
			indices[i++] = pos + 3;
			pos += 4;
		}
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.vertexIndexBuffer);
		gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, indices, gl.STATIC_DRAW);
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, null);


		this.scratchVertices = new Float32Array(12*nPoints);
	},

	setupTextures : function()
	{
		this.frameBuffer = gl.createFramebuffer();
		this.lineTexture = this.makeTexture(1024, 1024);
		this.onResize();
		this.blur1Texture = this.makeTexture(256, 256);
		this.blur2Texture = this.makeTexture(256, 256);
		this.blur3Texture = this.makeTexture(32, 32);
		this.blur4Texture = this.makeTexture(32, 32);
        this.screenTexture = Render.loadTexture('noise.jpg');
	},

	onResize : function()
	{
		var windowWidth = Math.max(document.documentElement.clientWidth, window.innerWidth || 0)
		var windowHeight = Math.max(document.documentElement.clientHeight, window.innerHeight || 0)
		var canvasSize = Math.min(windowHeight, windowWidth);
		Render.canvas.width = canvasSize;
		Render.canvas.height = canvasSize;
		if (Render.lineTexture)
		{
			var renderSize = Math.min(Math.max(canvasSize, 128), 1024);
			Render.lineTexture.width = renderSize;
			Render.lineTexture.height = renderSize;
			//testOutputElement.value = windowHeight;
		}

	},

	drawLineTexture: function (xPoints, yPoints, zPoints)
	{
		this.fadeAmount = Math.min(1, Math.pow(0.5, controls.persistence) * 0.4);
		this.activateTargetTexture(this.lineTexture);
		this.fade();
		//gl.clear(gl.COLOR_BUFFER_BIT);
		this.drawLine(xPoints, yPoints, zPoints);
		gl.bindTexture(gl.TEXTURE_2D, this.targetTexture);
		gl.generateMipmap(gl.TEXTURE_2D);
	},

	drawCRT : function()
	{
		this.setNormalBlending();

		this.activateTargetTexture(this.blur1Texture);
		this.setShader(this.texturedShader);
		gl.uniform1f(this.texturedShader.uResizeForCanvas, this.lineTexture.width/1024);
		this.drawTexture(this.lineTexture);

		//horizontal blur 256x256
		this.activateTargetTexture(this.blur2Texture);
		this.setShader(this.blurShader);
		gl.uniform2fv(this.blurShader.uOffset, [1.0/256.0, 0.0]);
		this.drawTexture(this.blur1Texture);

		//vertical blur 256x256
		this.activateTargetTexture(this.blur1Texture);
		//this.setShader(this.blurShader);
		gl.uniform2fv(this.blurShader.uOffset, [0.0, 1.0/256.0]);
		this.drawTexture(this.blur2Texture);

		//preserve blur1 for later
		this.activateTargetTexture(this.blur3Texture);
		this.setShader(this.texturedShader);
		gl.uniform1f(this.texturedShader.uResizeForCanvas, 1.0);
		this.drawTexture(this.blur1Texture);

		//horizontal blur 64x64
		this.activateTargetTexture(this.blur4Texture);
		this.setShader(this.blurShader);
		gl.uniform2fv(this.blurShader.uOffset, [1.0/32.0, 1.0/60.0]);
		this.drawTexture(this.blur3Texture);

		//vertical blur 64x64
		this.activateTargetTexture(this.blur3Texture);
		//this.setShader(this.blurShader);
		gl.uniform2fv(this.blurShader.uOffset, [-1.0/60.0, 1.0/32.0]);
		this.drawTexture(this.blur4Texture);

		this.activateTargetTexture(null);
		this.setShader(this.outputShader);
		var brightness = Math.pow(2, controls.brightness-2.0);
		//if (controls.disableFilter) brightness *= Filter.steps;
		gl.uniform1f(this.outputShader.uExposure, brightness);
        gl.uniform1f(this.outputShader.uSaturation, controls.saturation);
		gl.uniform1f(this.outputShader.uResizeForCanvas, this.lineTexture.width/1024);
		var colour = this.getColourFromHue(controls.hue);
		gl.uniform3fv(this.outputShader.uColour, colour);
		this.drawTexture(this.lineTexture, this.blur1Texture, this.blur3Texture, this.screenTexture);
	},

	getColourFromHue : function(hue)
	{
		var alpha = (hue/120.0) % 1.0;
		var start = Math.sqrt(1.0-alpha);
		var end = Math.sqrt(alpha);
		var colour;
		if (hue<120) colour = [start, end, 0.0];
		else if (hue<240) colour = [0.0, start, end];
		else colour = [end, 0.0, start];
		return colour;
	},

	activateTargetTexture : function(texture)
	{
		if (texture)
		{
			gl.bindFramebuffer(gl.FRAMEBUFFER, this.frameBuffer);
			gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
			gl.viewport(0, 0, texture.width, texture.height);
		}
		else
		{
			gl.bindFramebuffer(gl.FRAMEBUFFER, null);
			gl.viewport(0, 0, this.canvas.width, this.canvas.height);
		}
		this.targetTexture = texture;
	},

	setShader : function(program)
	{
		this.program = program;
		gl.useProgram(program);
	},

	drawTexture : function(texture0, texture1, texture2, texture3)
	{
		//gl.useProgram(this.program);
		gl.enableVertexAttribArray(this.program.aPos);

    	gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, texture0);
		gl.uniform1i(this.program.uTexture0, 0);

		if (texture1)
		{
			gl.activeTexture(gl.TEXTURE1);
			gl.bindTexture(gl.TEXTURE_2D, texture1);
			gl.uniform1i(this.program.uTexture1, 1);
		}

		if (texture2)
		{
			gl.activeTexture(gl.TEXTURE2);
			gl.bindTexture(gl.TEXTURE_2D, texture2);
			gl.uniform1i(this.program.uTexture2, 2);
		}

		if (texture3)
		{
			gl.activeTexture(gl.TEXTURE3);
			gl.bindTexture(gl.TEXTURE_2D, texture3);
			gl.uniform1i(this.program.uTexture3, 3);
		}

		gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer);
   		gl.bufferData(gl.ARRAY_BUFFER, this.fullScreenQuad, gl.STATIC_DRAW);
		gl.vertexAttribPointer(this.program.aPos, 2, gl.FLOAT, false, 0, 0);
		gl.bindBuffer(gl.ARRAY_BUFFER, null);

		gl.drawArrays(gl.TRIANGLES, 0, 6);
		gl.disableVertexAttribArray(this.program.aPos);

		if (this.targetTexture)
		{
			gl.bindTexture(gl.TEXTURE_2D, this.targetTexture);
			gl.generateMipmap(gl.TEXTURE_2D);
		}
	},

	drawLine : function(xPoints, yPoints, zPoints)
	{
		this.setAdditiveBlending();

		var scratchVertices = this.scratchVertices;
		//this.totalLength = 0;
		var nPoints = xPoints.length;
		for (var i=0; i<nPoints; i++)
		{
			var p = i * 12;
			scratchVertices[p]     = scratchVertices[p + 3] = scratchVertices[p + 6] = scratchVertices[p + 9]  = xPoints[i];
			scratchVertices[p + 1] = scratchVertices[p + 4] = scratchVertices[p + 7] = scratchVertices[p + 10] = yPoints[i];
			scratchVertices[p + 2] = scratchVertices[p + 5] = scratchVertices[p + 8] = scratchVertices[p + 11] = zPoints[i];
		}

		gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer);
		gl.bufferData(gl.ARRAY_BUFFER, scratchVertices, gl.STATIC_DRAW);
		gl.bindBuffer(gl.ARRAY_BUFFER, null);

		var program = this.lineShader;
		gl.useProgram(program);
		gl.enableVertexAttribArray(program.aStart);
		gl.enableVertexAttribArray(program.aEnd);
		gl.enableVertexAttribArray(program.aIdx);

		gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer);
		gl.vertexAttribPointer(program.aStart, 3, gl.FLOAT, false, 0, 0);
		gl.vertexAttribPointer(program.aEnd, 3, gl.FLOAT, false, 0, 12*4);
		gl.bindBuffer(gl.ARRAY_BUFFER, this.quadIndexBuffer);
		gl.vertexAttribPointer(program.aIdx, 1, gl.FLOAT, false, 0, 0);

    	gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, this.screenTexture);
		gl.uniform1i(program.uScreen, 0);

		gl.uniform1f(program.uSize, controls.focus);
		gl.uniform1f(program.uGain, Math.pow(2.0,controls.mainGain)*450/512);
		if (controls.invertXY) gl.uniform1f(program.uInvert, -1.0);
		else gl.uniform1f(program.uInvert, 1.0);

		var intensity = controls.intensity * (41000 / externalSampleRate);

		if (controls.disableFilter) gl.uniform1f(program.uIntensity, intensity *(Filter.steps+1.5));
		// +1.5 needed above for some reason for the brightness to match
		else gl.uniform1f(program.uIntensity, intensity);
		gl.uniform1f(program.uFadeAmount, this.fadeAmount);
		gl.uniform1f(program.uNEdges, this.nEdges);

		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.vertexIndexBuffer);
    	var nEdgesThisTime = (xPoints.length-1);

    	/*if (this.totalLength > 300)
    	{
    		nEdgesThisTime *= 300/this.totalLength;
    		nEdgesThisTime = Math.floor(nEdgesThisTime);
		}*/

    	gl.drawElements(gl.TRIANGLES, nEdgesThisTime * 6, gl.UNSIGNED_SHORT, 0);

		gl.disableVertexAttribArray(program.aStart);
		gl.disableVertexAttribArray(program.aEnd);
		gl.disableVertexAttribArray(program.aIdx);
	},

	fade : function(alpha)
	{
		this.setNormalBlending();

		var program = this.simpleShader;
		gl.useProgram(program);
		gl.enableVertexAttribArray(program.vertexPosition);
		gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer);
   		gl.bufferData(gl.ARRAY_BUFFER, this.fullScreenQuad, gl.STATIC_DRAW);
		gl.vertexAttribPointer(program.vertexPosition, 2, gl.FLOAT, false, 0, 0);
		gl.bindBuffer(gl.ARRAY_BUFFER, null);
		gl.uniform4fv(program.colour, [0.0, 0.0, 0.0, this.fadeAmount]);
		gl.drawArrays(gl.TRIANGLES, 0, 6);
		gl.disableVertexAttribArray(program.vertexPosition);
	},

	loadTexture : function(src)
	{
		var texture = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, texture);
		// Fill with grey pixel, as placeholder until loaded
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
              new Uint8Array([128, 128, 128, 255]));
		// Asynchronously load an image
		var image = new Image();
        image.crossOrigin = "anonymous";
		image.src = src;
		image.addEventListener('load', function()
		{
		  	// Now that the image has loaded make copy it to the texture.
		  	gl.bindTexture(gl.TEXTURE_2D, texture);
		  	gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
			//gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
			gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
			gl.generateMipmap(gl.TEXTURE_2D);
			//hardcoded:
			texture.width = texture.height = 512;
			if (controls.grid) Render.drawGrid(texture);
		});
		return texture;
	},

	drawGrid : function(texture)
	{
		this.activateTargetTexture(texture);
		this.setNormalBlending();
		this.setShader(this.simpleShader);
		gl.colorMask(true, false, false, true);

		var data = [];

		for (var i=0; i<11; i++)
		{
			var step = 45;
			var s = i*step;
			data.splice(0,0, 0, s, 10*step, s);
			data.splice(0,0, s, 0, s, 10*step);
			if (i!=0 && i!=10)
			{
				for (var j=0; j<51; j++)
				{
					t = j*step/5;
					if (i!=5)
					{
						data.splice(0,0, t, s-2, t, s+1);
						data.splice(0,0, s-2, t, s+1, t);
					}
					else
					{
						data.splice(0,0, t, s-5, t, s+4);
						data.splice(0,0, s-5, t, s+4, t);
					}
				}
			}
		}

		for (var j=0; j<51; j++)
		{
			var t = j*step/5;
			if (t%5 == 0) continue;
			data.splice(0,0, t-2, 2.5*step, t+2, 2.5*step);
			data.splice(0,0, t-2, 7.5*step, t+2, 7.5*step);
		}


		var vertices = new Float32Array(data);
		for (var i=0; i<data.length; i++)
		{
			vertices[i]=(vertices[i]+31)/256-1;
		}


		gl.enableVertexAttribArray(this.program.vertexPosition);
		gl.bindBuffer(gl.ARRAY_BUFFER, this.vertexBuffer);
   		gl.bufferData(gl.ARRAY_BUFFER, vertices, gl.STATIC_DRAW);
		gl.vertexAttribPointer(this.program.vertexPosition, 2, gl.FLOAT, false, 0, 0);
		gl.bindBuffer(gl.ARRAY_BUFFER, null);
		gl.uniform4fv(this.program.colour, [0.01, 0.1, 0.01, 1.0]);

		gl.lineWidth(1.0);
		gl.drawArrays(gl.LINES, 0, vertices.length/2);

		gl.bindTexture(gl.TEXTURE_2D, this.targetTexture);
		gl.generateMipmap(gl.TEXTURE_2D);
		gl.colorMask(true, true, true, true);
	},

	makeTexture : function(width, height)
	{
		var texture = gl.createTexture();
		gl.bindTexture(gl.TEXTURE_2D, texture);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.FLOAT, null);
		//gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, Render.ext.HALF_FLOAT_OES, null);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
		//gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		gl.generateMipmap(gl.TEXTURE_2D);
		gl.bindTexture(gl.TEXTURE_2D, null);
		texture.width = width;
		texture.height = height;
		return texture;
	},

	xactivateTargetTexture : function(ctx, texture)
	{
		gl.bindRenderbuffer(gl.RENDERBUFFER, ctx.renderBuffer);
		gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT16, ctx.frameBuffer.width, ctx.frameBuffer.height);

		gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
		gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, ctx.renderBuffer);

		gl.bindTexture(gl.TEXTURE_2D, null);
		gl.bindRenderbuffer(gl.RENDERBUFFER, null);
	},

	setAdditiveBlending : function()
	{
		//gl.blendEquation( gl.FUNC_ADD );
		gl.blendFunc(gl.ONE, gl.ONE);
	},

	setNormalBlending : function()
	{
		//gl.blendEquation( gl.FUNC_ADD );
		gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
	},

	createShader : function(vsTag, fsTag)
	{
		if (!this.supportsWebGl())
		{
			throw new Error('createShader: no WebGL context');
		}

		var vsSource = document.getElementById(vsTag).firstChild.nodeValue;
		var fsSource = document.getElementById(fsTag).firstChild.nodeValue;

		var vs = gl.createShader(gl.VERTEX_SHADER);
		gl.shaderSource(vs, vsSource);
		gl.compileShader(vs);
		if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS))
		{
			var infoLog = gl.getShaderInfoLog(vs);
			gl.deleteShader(vs);
			throw new Error('createShader, vertex shader compilation:\n' + infoLog);
		}

		var fs = gl.createShader(gl.FRAGMENT_SHADER);
		gl.shaderSource(fs, fsSource);
		gl.compileShader(fs);
		if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS))
		{
			var infoLog = gl.getShaderInfoLog(fs);
			gl.deleteShader(vs);
			gl.deleteShader(fs);
			throw new Error('createShader, fragment shader compilation:\n' + infoLog);
		}

		var program = gl.createProgram();

		gl.attachShader(program, vs);
		gl.deleteShader(vs);

		gl.attachShader(program, fs);
		gl.deleteShader(fs);

		gl.linkProgram(program);

		if (!gl.getProgramParameter(program, gl.LINK_STATUS))
		{
			var infoLog = gl.getProgramInfoLog(program);
			gl.deleteProgram(program);
			throw new Error('createShader, linking:\n' + infoLog);
		}

		return program;
	},

	supportsWebGl : function()
	{
		// from https://github.com/Modernizr/Modernizr/blob/master/feature-detects/webgl.js
		var canvas = document.createElement('canvas'),
			supports = 'probablySupportsContext' in canvas ? 'probablySupportsContext' : 'supportsContext';
		if (supports in canvas)
		{
			return canvas[supports]('webgl') || canvas[supports]('experimental-webgl');
		}
		return 'WebGLRenderingContext' in window;
	}
}

var sweepPosition = -1;
var belowTrigger = false;

function doScriptProcessor(bufferBase64) {
	var req = new XMLHttpRequest;
	req.open('GET', "data:application/octet;base64," + bufferBase64);
	req.responseType = 'arraybuffer';
	req.onload = function fileLoaded(e) {
		Juce.getNativeFunction("getSettings")().then(settings => {
			controls.brightness = settings.brightness;
			controls.intensity = settings.intensity;
			controls.persistence = settings.persistence;
            controls.saturation = settings.saturation;
            controls.focus = settings.focus;
			controls.hue = settings.hue;
			controls.disableFilter = !settings.upsampling;
			let numChannels = settings.numChannels;

			if (controls.grid !== settings.graticule) {
				controls.grid = settings.graticule;
				const image = controls.noise ? 'noise.jpg' : 'empty.jpg';
				Render.screenTexture = Render.loadTexture(image);
			}

			if (controls.noise !== settings.smudges) {
				controls.noise = settings.smudges;
				const image = controls.noise ? 'noise.jpg' : 'empty.jpg';
				Render.screenTexture = Render.loadTexture(image);
			}

			var dataView = new DataView(e.target.response);

			const stride = 4 * numChannels;
			for (var i = 0; i < xSamples.length; i++) {
				xSamples[i] = dataView.getFloat32(i * stride, true);
				ySamples[i] = dataView.getFloat32(i * stride + 4, true);
				if (numChannels === 3) {
					zSamples[i] = dataView.getFloat32(i * stride + 8, true);
				} else {
					zSamples[i] = 1;
				}
			}

			if (controls.sweepOn) {
				var gain = Math.pow(2.0, controls.mainGain);
				var sweepMinTime = controls.sweepMsDiv * 10 / 1000;
				var triggerValue = controls.sweepTriggerValue;
				for (var i = 0; i < xSamples.length; i++) {
					xSamples[i] = sweepPosition / gain;
					sweepPosition += 2 * AudioSystem.timePerSample / sweepMinTime;
					if (sweepPosition > 1.1 && belowTrigger && ySamples[i] >= triggerValue)
						sweepPosition = -1.3;
					belowTrigger = ySamples[i] < triggerValue;
				}
			}

			if (!controls.freezeImage) {
				if (!controls.disableFilter) {
					Filter.generateSmoothedSamples(AudioSystem.oldXSamples, xSamples, AudioSystem.smoothedXSamples);
					Filter.generateSmoothedSamples(AudioSystem.oldYSamples, ySamples, AudioSystem.smoothedYSamples);
					if (numChannels === 3) {
						Filter.generateSmoothedSamples(AudioSystem.oldZSamples, zSamples, AudioSystem.smoothedZSamples);
					} else {
						AudioSystem.smoothedZSamples.fill(1);
					}

					if (!controls.swapXY) Render.drawLineTexture(AudioSystem.smoothedXSamples, AudioSystem.smoothedYSamples, AudioSystem.smoothedZSamples);
					else Render.drawLineTexture(AudioSystem.smoothedYSamples, AudioSystem.smoothedXSamples, AudioSystem.smoothedZSamples);
				}
				else {
					if (!controls.swapXY) Render.drawLineTexture(xSamples, ySamples, zSamples);
					else Render.drawLineTexture(ySamples, xSamples, zSamples);
				}
			}

			for (var i = 0; i < xSamples.length; i++) {
				AudioSystem.oldXSamples[i] = xSamples[i];
				AudioSystem.oldYSamples[i] = ySamples[i];
				AudioSystem.oldZSamples[i] = zSamples[i];
			}

			requestAnimationFrame(drawCRTFrame);
		});
	}
	req.send();
}

function drawCRTFrame(timeStamp) {
	Render.drawCRT();
}
                                           
var xSamples = new Float32Array(externalBufferSize);
var ySamples = new Float32Array(externalBufferSize);
var zSamples = new Float32Array(externalBufferSize);

Juce.getNativeFunction("bufferSize")().then(bufferSize => {
    externalBufferSize = bufferSize;
    Juce.getNativeFunction("sampleRate")().then(sampleRate => {
		externalSampleRate = sampleRate;
        xSamples = new Float32Array(externalBufferSize);
		ySamples = new Float32Array(externalBufferSize);
		zSamples = new Float32Array(externalBufferSize);
        Render.init();
        Filter.init(externalBufferSize, 8, 6);
        AudioSystem.init(externalBufferSize);
        Render.setupArrays(Filter.nSmoothedSamples);
        AudioSystem.startSound();
        requestAnimationFrame(drawCRTFrame);
    });
});
                        


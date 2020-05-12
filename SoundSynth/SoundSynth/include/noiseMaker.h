#pragma once

#pragma comment(lib, "winmm.lib")

#include <iostream>
#include <cmath>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <Windows.h>
#include <condition_variable>

const double PI = 2.0 * acos(0.0);


template <class T>
class NoiseMaker
{

	double(*m_userFunction)(double);
	unsigned int sampleRate;
	unsigned int blockNum;
	unsigned int channels;
	unsigned int blockSamples;
	unsigned int blockCurrent;
	WAVEHDR *waveHeaders;
	HWAVEOUT hwDevice;
	std::thread threadz;
	std::atomic<bool> ready;
	std::atomic<unsigned int> blockFree;
	std::condition_variable cvNonZeroBlock;
	std::mutex muxNonZeroBlock;
	T* blockMemory;
	std::atomic<double> globalTime;
	void waveOutProc(HWAVEOUT hWaveOut, UINT uMsg, DWORD dwParam1, DWORD dwParam2)
	{
		if (uMsg != WOM_DONE)
			return;
		blockFree++;
		std::unique_lock<std::mutex> lm(muxNonZeroBlock);
		cvNonZeroBlock.notify_one();
	}

	static void CALLBACK waveOutProcWrap(HWAVEOUT hWaveOut, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
	{
		((NoiseMaker*)dwInstance)->waveOutProc(hWaveOut, uMsg, dwParam1, dwParam2);
	}


	void MainThread()
	{
		globalTime = 0.0;
		double timeStep = 1.0 / (double)sampleRate;

		//Silly hack to get maximum integer for a type at runtime
		T maxSample = (T)pow(2, (sizeof(T) * 8) - 1) - 1;
		double dMaxSample = (double)maxSample;
		T prevSample = 0;

		while (ready)
		{
			if (blockFree == 0)
			{
				std::unique_lock<std::mutex> lm(muxNonZeroBlock);
				cvNonZeroBlock.wait(lm);
			}
			blockFree--;

			//prepare block for processing
			if (waveHeaders[blockCurrent].dwFlags & WHDR_PREPARED)
				waveOutUnprepareHeader(hwDevice, &waveHeaders[blockCurrent], sizeof(WAVEHDR));

			T newSample = 0;
			int currentBlock = blockCurrent * blockSamples;

			for (unsigned int i = 0; i < blockSamples; i++)
			{
				if (m_userFunction == nullptr)
					newSample = (T)(clip(UserProcess(globalTime), 1.0) * dMaxSample);
				else
					newSample = (T)(clip(m_userFunction(globalTime), 1.0) * dMaxSample);

				blockMemory[blockCurrent + i] = newSample;
				prevSample = newSample;
				globalTime = globalTime + timeStep;
			}

			//Send block to sound device
			waveOutPrepareHeader(hwDevice, &waveHeaders[blockCurrent], sizeof(WAVEHDR));
			waveOutWrite(hwDevice, &waveHeaders[blockCurrent], sizeof(WAVEHDR));
			blockCurrent++;
			blockCurrent %= blockNum;
		}
	}

public:
		NoiseMaker(std::wstring outputDevice_, unsigned int samplRate_ = 44100, unsigned int channels_ = 1, unsigned int blocks_ = 8, unsigned int blockSamples_ = 512)
		{
			Create(outputDevice_, samplRate_, channels_, blocks_, blockSamples_);
		}
		~NoiseMaker()
		{
			Destroy();
		}
		bool Create(std::wstring outputDevice_, unsigned int samplRate_ = 44100, unsigned int channels_ = 1, unsigned int blocks_ = 8, unsigned int blockSamples_ = 512)
		{
			ready = false;
			sampleRate = samplRate_;
			channels = channels_;
			blockSamples = blockSamples_;
			blockNum = blocks_;
			blockCurrent = 0;
			blockMemory = nullptr;
			waveHeaders = nullptr;
			m_userFunction = nullptr;

			//Device Validation

			std::vector<std::wstring> devices = Enumerate();
			auto d = find(devices.begin(), devices.end(), outputDevice_);
			if (d != devices.end())
			{
				int devId = std::distance(devices.begin(), d);
				WAVEFORMATEX waveFormat;
				waveFormat.wFormatTag = WAVE_FORMAT_PCM;
				waveFormat.nSamplesPerSec = sampleRate;
				waveFormat.wBitsPerSample = sizeof(T) * 8;
				waveFormat.nChannels = channels;
				waveFormat.nBlockAlign = (waveFormat.wBitsPerSample / 8) * waveFormat.nChannels;
				waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
				waveFormat.cbSize = 0;



				//Open device
				if (waveOutOpen(&hwDevice, devId, &waveFormat, (DWORD_PTR)waveOutProcWrap, (DWORD_PTR)this, CALLBACK_FUNCTION) != S_OK)
					return Destroy();
			}

			blockMemory = new T[blockNum * blockSamples];
			if (blockMemory == nullptr)
				return Destroy();
			ZeroMemory(blockMemory, sizeof(T) * blockNum * blockSamples);

			waveHeaders = new WAVEHDR[blockNum];
			if (waveHeaders == nullptr)
				return Destroy();
			ZeroMemory(waveHeaders, sizeof(WAVEHDR) * blockNum);

			//Linking headers to block memory

			for (unsigned int i = 0; i < blockNum; i++)
			{
				waveHeaders[i].dwBufferLength = blockSamples * sizeof(T);
				waveHeaders[i].lpData = (LPSTR)(blockMemory + (i * blockSamples));
			}
			ready = true;
			threadz = std::thread(&NoiseMaker::MainThread, this);

			std::unique_lock<std::mutex> lm(muxNonZeroBlock);
			cvNonZeroBlock.notify_one();
			return true;
		}
		bool Destroy()
		{
			return false;
		}

		void Stop()
		{
			ready = false;
			threadz.join();
		}

		// Override to process current sample
		virtual double UserProcess(double dTime)
		{
			return 0.0;
		}

		double GetTime()
		{
			return globalTime;
		}

public:	
		static std::vector<std::wstring> Enumerate()
		{
			int deviceCount = waveOutGetNumDevs();
			std::vector<std::wstring> sDevices;
			WAVEOUTCAPS woc;
			for (int n = 0; n < deviceCount; n++)
				if (waveOutGetDevCaps(n, &woc, sizeof(WAVEOUTCAPS)) == S_OK)
					sDevices.push_back(woc.szPname);
			return sDevices;
		}

		void setUserFunction(double(*func)(double))
		{
			m_userFunction = func;
		}

		double clip(double dSample, double dMax)
		{
			if (dSample >= 0.0)
				return fmin(dSample, dMax);
			else
				return fmax(dSample, -dMax);
		}
};

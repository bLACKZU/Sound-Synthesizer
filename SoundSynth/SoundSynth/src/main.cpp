#include <iostream>
#include <string>
#include "noiseMaker.h"

double makeNoise(double dTime)
{
	double output = 1.0 * sin(220.0 * 2 * 3.14159 * dTime);
	if (output > 0.0)
		return 0.2;
	else
		return -0.2;
}

int main()
{
	std::wcout << "Synth P1" << std::endl;

	//Get all sound hardware
	std::vector<std::wstring> devices = NoiseMaker<short>::Enumerate();
										
	//display all devices
	for (auto d : devices)
		std::wcout << "Found output device" << d << std::endl;																															
	NoiseMaker<short> sound(devices[0], 44100, 1, 8, 512);

	sound.setUserFunction(makeNoise);

	while (1)
	{

	}





	return 0;	
}								
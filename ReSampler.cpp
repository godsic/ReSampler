/*
* Copyright (C) 2016 - 2019 Judd Niemann - All Rights Reserved.
* You may use, distribute and modify this code under the
* terms of the GNU Lesser General Public License, version 2.1
*
* You should have received a copy of GNU Lesser General Public License v2.1
* with this file. If not, please refer to: https://github.com/jniemann66/ReSampler
*/

// ReSampler.cpp : Audio Sample Rate Converter by Judd Niemann.

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <string>
#include <iostream>
#include <vector>
#include <iomanip>
#include <regex>

#ifdef __APPLE__
#include <unistd.h>
#include <libproc.h>
#endif

#if defined (__MINGW64__) || defined (__MINGW32__) || defined (__GNUC__)
#ifdef USE_QUADMATH
#include <quadmath.h>
#ifndef FIR_QUAD_PRECISION
#define FIR_QUAD_PRECISION
#endif
#endif
#endif

#include "csv.h" // to-do: check macOS
#include "ReSampler.h"
#include "conversioninfo.h"
#include "osspecific.h"
#include "ctpl/ctpl_stl.h"
#include "dsf.h"
#include "dff.h"
#include "raiitimer.h"
#include "fraction.h"
#include "srconvert.h"
#if !defined(__ANDROID__) && !defined(__arm__) && !defined(__aarch64__)
#else
#define COMPILING_ON_ANDROID
#include <Android/log.h>
#define LOG_TAG "ReSampler"
#define ANDROID_OUT(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define ANDROID_ERR(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ANDROID_STDTOC(x) x.c_str()
#endif
////////////////////////////////////////////////////////////////////////////////////////
// This program uses the following libraries:
// 1:
// libsndfile
// available at http://www.mega-nerd.com/libsndfile/
//
// (copy of entire package included in $(ProjectDir)\libsbdfile)
//
// 2:
// fftw
// http://www.fftw.org/
//
//                                                                                    //
////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char * argv[])
{
	// test for global options
	if (parseGlobalOptions(argc, argv)) {
		exit(EXIT_SUCCESS);
	}

	// ConversionInfo instance to hold parameters
	ConversionInfo ci;

	// get path/name of this app
#ifdef __APPLE__
	char pathBuf[PROC_PIDPATHINFO_MAXSIZE];
	pid_t pid = getpid();
	if ( proc_pidpath (pid, pathBuf, sizeof(pathBuf)) == 0 ) {
		ci.appName.assign(pathBuf);
	}
#else
	ci.appName = argv[0];
#endif

	ci.overSamplingFactor = 1;

	// get conversion parameters
	ci.fromCmdLineArgs(argc, argv);
	if (ci.bBadParams) {
		exit(EXIT_FAILURE);
	}

	// query build version AND cpu
	if (!showBuildVersion())
		exit(EXIT_FAILURE); // can't continue (CPU / build mismatch)

	// echo filenames to user
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("Input file: %s", ANDROID_STDTOC(ci.inputFilename));
    ANDROID_OUT("Output file: %s", ANDROID_STDTOC(ci.outputFilename));
#else
	std::cout << "Input file: " << ci.inputFilename << std::endl;
	std::cout << "Output file: " << ci.outputFilename << std::endl;
#endif
	if (ci.disableClippingProtection) {
#ifdef COMPILING_ON_ANDROID
	    ANDROID_OUT("clipping protection disabled ");
#else
		std::cout << "clipping protection disabled " << std::endl;
#endif
	}

	// Isolate the file extensions
	std::string inFileExt;
	std::string outFileExt;
	if (ci.inputFilename.find_last_of('.') != std::string::npos)
		inFileExt = ci.inputFilename.substr(ci.inputFilename.find_last_of('.') + 1);
	if (ci.outputFilename.find_last_of('.') != std::string::npos)
		outFileExt = ci.outputFilename.substr(ci.outputFilename.find_last_of('.') + 1);

	// detect dsf or dff format
	ci.dsfInput = (inFileExt == "dsf");
	ci.dffInput = (inFileExt == "dff");

	// detect csv output
	ci.csvOutput = (outFileExt == "csv");

	if (ci.csvOutput) {
#ifdef COMPILING_ON_ANDROID
	    ANDROID_OUT("Outputting to csv format");
#else
        std::cout << "Outputting to csv format" << std::endl;
#endif
	}
	else {
		if (!ci.outBitFormat.empty()) {  // new output bit format requested
			ci.outputFormat = determineOutputFormat(outFileExt, ci.outBitFormat);
			if (ci.outputFormat) {
#ifdef COMPILING_ON_ANDROID
                ANDROID_OUT("Changing output bit format to %s", ANDROID_STDTOC(ci.outBitFormat));
#else
                std::cout << ci.outBitFormat << std::endl;
#endif
			}
			else { // user-supplied bit format not valid; try choosing appropriate format
				determineBestBitFormat(ci.outBitFormat, ci.inputFilename, ci.outputFilename);
				ci.outputFormat = determineOutputFormat(outFileExt, ci.outBitFormat);
				if (ci.outputFormat) {
#ifdef COMPILING_ON_ANDROID
                    ANDROID_OUT("Changing output bit format to %s", ANDROID_STDTOC(ci.outBitFormat));
#else
                    std::cout << "Changing output bit format to " << ci.outBitFormat << std::endl;
#endif
                }
				else {
#ifdef COMPILING_ON_ANDROID
                    ANDROID_OUT("Warning: NOT Changing output file bit format !");
#else
					std::cout << "Warning: NOT Changing output file bit format !" << std::endl;
#endif
					ci.outputFormat = 0; // back where it started
				}
			}
		}

		if (outFileExt != inFileExt)
		{ // file extensions differ, determine new output format:

			if (ci.outBitFormat.empty()) { // user changed file extension only. Attempt to choose appropriate output sub format:
#ifdef COMPILING_ON_ANDROID
			    ANDROID_OUT("Output Bit Format not specified");
#else
				std::cout << "Output Bit Format not specified" << std::endl;
#endif
				determineBestBitFormat(ci.outBitFormat, ci.inputFilename, ci.outputFilename);
			}
			ci.outputFormat = determineOutputFormat(outFileExt, ci.outBitFormat);
			if (ci.outputFormat) {
#ifdef COMPILING_ON_ANDROID
			    ANDROID_OUT("Changing output file format to %s", ANDROID_STDTOC(outFileExt));
#else
                std::cout << "Changing output file format to " << outFileExt << std::endl;
#endif
            }
			else { // cannot determine subformat of output file
#ifdef COMPILING_ON_ANDROID
                ANDROID_OUT("Warning: NOT Changing output file format ! (extension different, but format will remain the same)");
#else
				std::cout << "Warning: NOT Changing output file format ! (extension different, but format will remain the same)" << std::endl;
#endif
			}
		}
	}
	try {

		if (ci.bUseDoublePrecision) {

#ifdef USE_QUADMATH
    #ifdef COMPILING_ON_ANDROID
            ANDROID_OUT("Using quadruple-precision for calculations.");
    #else
            std::cout << "Using quadruple-precision for calculations." << std::endl;
    #endif
#else
    #ifdef COMPILING_ON_ANDROID
            ANDROID_OUT("Using double precision for calculations.");
    #else
			std::cout << "Using double precision for calculations." << std::endl;
    #endif
#endif
			if (ci.dsfInput) {
				ci.bEnablePeakDetection = false;
				return convert<DsfFile, double> (ci) ? EXIT_SUCCESS : EXIT_FAILURE;
			}
			else if (ci.dffInput) {
				ci.bEnablePeakDetection = false;
				return convert<DffFile, double> (ci) ? EXIT_SUCCESS : EXIT_FAILURE;
			}
			else {
				ci.bEnablePeakDetection = true;
				return convert<SndfileHandle, double> (ci) ? EXIT_SUCCESS : EXIT_FAILURE;
			}
		}

		else {

#ifdef USE_QUADMATH
            #ifdef COMPILING_ON_ANDROID
            ANDROID_OUT("Using quadruple-precision for calculations.");
    #else
            std::cout << "Using quadruple-precision for calculations." << std::endl;
    #endif
#endif
			if (ci.dsfInput) {
				ci.bEnablePeakDetection = false;
				return convert<DsfFile, float> (ci) ? EXIT_SUCCESS : EXIT_FAILURE;
			}
			else if (ci.dffInput) {
				ci.bEnablePeakDetection = false;
				return convert<DffFile, float> (ci) ? EXIT_SUCCESS : EXIT_FAILURE;
			}
			else {
				ci.bEnablePeakDetection = true;
				return convert<SndfileHandle, float> (ci) ? EXIT_SUCCESS : EXIT_FAILURE;
			}
		}

	} //ends try block

	catch (const std::exception& e) {
#ifdef COMPILING_ON_ANDROID
        ANDROID_ERR("fatal error: %s", e.what());
#else
		std::cerr << "fatal error: " << e.what();
#endif
		return EXIT_FAILURE;
	}
}

// parseGlobalOptions() - result indicates whether to terminate.
bool parseGlobalOptions(int argc, char * argv[]) {

	// help switch:
	if (getCmdlineParam(argv, argv + argc, "--help") || getCmdlineParam(argv, argv + argc, "-h")) {
#ifdef COMPILING_ON_ANDROID
        ANDROID_OUT("%s", ANDROID_STDTOC(strUsage));
        ANDROID_OUT("Additional options:\n\n%s", ANDROID_STDTOC(strExtraOptions));
#else
		std::cout << strUsage << std::endl;
		std::cout << "Additional options:\n\n" << strExtraOptions << std::endl;
#endif
		return true;
	}

	// version switch:
	if (getCmdlineParam(argv, argv + argc, "--version")) {
#ifdef COMPILING_ON_ANDROID
	    ANDROID_OUT("%s", ANDROID_STDTOC(strVersion));
#else
		std::cout << strVersion << std::endl;
#endif
		return true;
	}

	// compiler switch:
	if (getCmdlineParam(argv, argv + argc, "--compiler")) {
		showCompiler();
		return true;
	}

	// sndfile-version switch:
	if (getCmdlineParam(argv, argv + argc, "--sndfile-version")) {
		char s[128];
		sf_command(nullptr, SFC_GET_LIB_VERSION, s, sizeof(s));
#ifdef COMPILING_ON_ANDROID
        ANDROID_OUT("%s", s);
#else
		std::cout << s << std::endl;
#endif
		return true;
	}

	// listsubformats
	if (getCmdlineParam(argv, argv + argc, "--listsubformats")) {
		std::string filetype;
		getCmdlineParam(argv, argv + argc, "--listsubformats", filetype);
		listSubFormats(filetype);
		return true;
	}

	// showDitherProfiles
	if (getCmdlineParam(argv, argv + argc, "--showDitherProfiles")) {
		showDitherProfiles();
		return true;
	}

	// generate
	if (getCmdlineParam(argv, argv + argc, "--generate")) {
		std::string filename;
		getCmdlineParam(argv, argv + argc, "--generate", filename);
		generateExpSweep(filename);
		return true;
	}

	return false;
}

// determineBestBitFormat() : determines the most appropriate bit format for the output file, through the following process:
// 1. Try to use infile's format and if that isn't valid for outfile, then:
// 2. use the default subformat for outfile.
// store best bit format as a string in BitFormat

bool determineBestBitFormat(std::string& BitFormat, const std::string& inFilename, const std::string& outFilename)
{
	// get infile's extension from filename:
	std::string inFileExt;
	if (inFilename.find_last_of('.') != std::string::npos)
		inFileExt = inFilename.substr(inFilename.find_last_of('.') + 1);

	bool dsfInput = false;
	bool dffInput = false;

	int inFileFormat = 0;

	if (inFileExt == "dsf") {
		dsfInput = true;
	}
	else if (inFileExt == "dff") {
		dffInput = true;
	}

	else { // libsndfile-openable file

		// Inspect input file for format:
		SndfileHandle infile(inFilename, SFM_READ);
		inFileFormat = infile.format();

		if (int e = infile.error()) {
#ifdef COMPILING_ON_ANDROID
            ANDROID_OUT("Couldn't Open Input File (%s)", sf_error_number(e));
#else
			std::cout << "Couldn't Open Input File (" << sf_error_number(e) << ")" << std::endl;
#endif
			return false;
		}

		// get BitFormat of inFile as a string:
		for (auto& subformat : subFormats) {
			if (subformat.second == (inFileFormat & SF_FORMAT_SUBMASK)) {
				BitFormat = subformat.first;
				break;
			}
		}

		// retrieve infile's TRUE extension (from the file contents), and if retrieval is successful, override extension derived from filename:
		SF_FORMAT_INFO infileFormatInfo;
		infileFormatInfo.format = inFileFormat & SF_FORMAT_TYPEMASK;
		if (sf_command(nullptr, SFC_GET_FORMAT_INFO, &infileFormatInfo, sizeof(infileFormatInfo)) == 0) {
			inFileExt = std::string(infileFormatInfo.extension);
		}
	}

	// get outfile's extension:
	std::string outFileExt;
	if (outFilename.find_last_of('.') != std::string::npos)
		outFileExt = outFilename.substr(outFilename.find_last_of('.') + 1);

	// when the input file is dsf/dff, use default output subformat:
	if (dsfInput || dffInput) { // choose default output subformat for chosen output file format
		BitFormat = defaultSubFormats.find(outFileExt)->second;
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("defaulting to %s", ANDROID_STDTOC(BitFormat));
#else
		std::cout << "defaulting to " << BitFormat << std::endl;
#endif
		return true;
	}

	// get total number of major formats:
	SF_FORMAT_INFO formatinfo;
	int format, major_count;
	memset(&formatinfo, 0, sizeof(formatinfo));
	sf_command(nullptr, SFC_GET_FORMAT_MAJOR_COUNT, &major_count, sizeof(int));

	// determine if inFile's subformat is valid for outFile:
	for (int m = 0; m < major_count; m++)
	{
		formatinfo.format = m;
		sf_command(nullptr, SFC_GET_FORMAT_MAJOR, &formatinfo, sizeof(formatinfo));

		if (stricmp(formatinfo.extension, outFileExt.c_str()) == 0) { // match between format number m and outfile's file extension
			format = formatinfo.format | (inFileFormat & SF_FORMAT_SUBMASK); // combine outfile's major format with infile's subformat

			// Check if format / subformat combination is valid:
			SF_INFO sfinfo;
			memset(&sfinfo, 0, sizeof(sfinfo));
			sfinfo.channels = 1;
			sfinfo.format = format;

			if (sf_format_check(&sfinfo)) { // Match: infile's subformat is valid for outfile's format
				break;
			} else { // infile's subformat is not valid for outfile's format; use outfile's default subformat
#ifdef COMPILING_ON_ANDROID
                ANDROID_OUT( "Output file format %s and subformat %s combination not valid ... ", ANDROID_STDTOC(outFileExt), ANDROID_STDTOC(BitFormat));
#else
				std::cout << "Output file format " << outFileExt << " and subformat " << BitFormat << " combination not valid ... ";
#endif
				BitFormat.clear();
				BitFormat = defaultSubFormats.find(outFileExt)->second;
#ifdef COMPILING_ON_ANDROID
		        ANDROID_OUT("defaulting to %s", ANDROID_STDTOC(BitFormat));
#else
				std::cout << "defaulting to " << BitFormat << std::endl;
#endif
				break;
			}
		}
	}
	return true;
}

// determineOutputFormat() : returns an integer representing the output format, which libsndfile understands:
int determineOutputFormat(const std::string& outFileExt, const std::string& bitFormat)
{
	SF_FORMAT_INFO info;
	int format = 0;
	int major_count;
	memset(&info, 0, sizeof(info));
	sf_command(nullptr, SFC_GET_FORMAT_MAJOR_COUNT, &major_count, sizeof(int));
	bool bFileExtFound = false;

	// Loop through all major formats to find match for outFileExt:
	for (int m = 0; m < major_count; ++m) {
		info.format = m;
		sf_command(nullptr, SFC_GET_FORMAT_MAJOR, &info, sizeof(info));
		if (stricmp(info.extension, outFileExt.c_str()) == 0) {
			bFileExtFound = true;
			break;
		}
	}

	if (bFileExtFound) {
		// Check if subformat is recognized:
		auto sf = subFormats.find(bitFormat);
		if (sf != subFormats.end())
			format = info.format | sf->second;
		else
#ifdef COMPILING_ON_ANDROID
		    ANDROID_OUT("Warning: bit format %s not recognised !", ANDROID_STDTOC(bitFormat));
#else
			std::cout << "Warning: bit format " << bitFormat << " not recognised !" << std::endl;
#endif
	}

	// Special cases:
	if (bitFormat == "8") {
		// user specified 8-bit. Determine whether it must be unsigned or signed, based on major type:
		// These formats always use unsigned: 8-bit when they use 8-bit: mat rf64 voc w64 wav

		if ((outFileExt == "mat") || (outFileExt == "rf64") || (outFileExt == "voc") || (outFileExt == "w64") || (outFileExt == "wav"))
			format = info.format | SF_FORMAT_PCM_U8;
		else
			format = info.format | SF_FORMAT_PCM_S8;
	}

	return format;
}

// listSubFormats() - lists all valid subformats for a given file extension (without "." or "*."):
void listSubFormats(const std::string& f)
{
	SF_FORMAT_INFO	info;
	int major_count;
	memset(&info, 0, sizeof(info));
	sf_command(nullptr, SFC_GET_FORMAT_MAJOR_COUNT, &major_count, sizeof(int));
	bool bFileExtFound = false;

	// Loop through all major formats to find match for outFileExt:
	for (int m = 0; m < major_count; ++m) {
		info.format = m;
		sf_command(nullptr, SFC_GET_FORMAT_MAJOR, &info, sizeof(info));
		if (stricmp(info.extension, f.c_str()) == 0) {
			bFileExtFound = true;
			break;
		}
	}

	if (bFileExtFound) {
		SF_INFO sfinfo;
		memset(&sfinfo, 0, sizeof(sfinfo));
		sfinfo.channels = 1;

		// loop through all subformats and find which ones are valid for file type:
		for (auto& subformat : subFormats) {
			sfinfo.format = (info.format & SF_FORMAT_TYPEMASK) | subformat.second;
			if (sf_format_check(&sfinfo))
#ifdef COMPILING_ON_ANDROID
		        ANDROID_OUT("%s", ANDROID_STDTOC(subformat.first));
#else
				std::cout << subformat.first << std::endl;
#endif
		}
	}
	else {
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("File extension %s unknown", ANDROID_STDTOC(f));
#else
		std::cout << "File extension " << f << " unknown" << std::endl;
#endif
	}
}

// convert()

/* Note: type 'FileReader' MUST implement the following methods:
constuctor(const std::string& fileName)
bool error() // or int error()
unsigned int channels()
unsigned int samplerate()
uint64_t frames()
int format()
read(inbuffer, count)
seek(position, whence)
*/

template<typename FileReader, typename FloatType>
bool convert(ConversionInfo& ci)
{
	bool multiThreaded = ci.bMultiThreaded;

	// pointer for temp file;
	SndfileHandle* tmpSndfileHandle = nullptr;

	// filename for temp file;
	std::string tmpFilename;

	// Open input file:
	// TODO: FOR RAW AUDIO FILES: pass correct arguments to LIBSNDFILE: SndfileHandle::SndfileHandle (std::string const & path, int mode, int fmt, int chans, int srate)
	// TODO: look in sndfile.hh:80 for possible constructors
	/*
	public :
        SndfileHandle (void) : p (nullptr) {} ;
        SndfileHandle (const char *path, int mode = SFM_READ,
        int format = 0, int channels = 0, int samplerate = 0) ;
        SndfileHandle (std::string const & path, int mode = SFM_READ,
            int format = 0, int channels = 0, int samplerate = 0) ;
        SndfileHandle (int fd, bool close_desc, int mode = SFM_READ,
            int format = 0, int channels = 0, int samplerate = 0) ;
        SndfileHandle (SF_VIRTUAL_IO &sfvirtual, void *user_data, int mode = SFM_READ,
            int format = 0, int channels = 0, int samplerate = 0) ;
	 */
	FileReader infile(ci.inputFilename);
	if (int e = infile.error()) {
#ifdef COMPILING_ON_ANDROID
		ANDROID_ERR("Error: Couldn't Open Input File (%s)", sf_error_number(e));
#else
		std::cerr << "Error: Couldn't Open Input File (" << sf_error_number(e) << ")" << std::endl; // to-do: make this more specific (?)
#endif
		return false;
	}

	// read input file metadata:
	MetaData m;
	getMetaData(m, infile);

	// read input file properties:
    int nChannels = static_cast<int>(infile.channels());
	ci.inputSampleRate = infile.samplerate();
	sf_count_t inputFrames = infile.frames();
	sf_count_t inputSampleCount = inputFrames * nChannels;
	double inputDuration = 1000.0 * inputFrames / ci.inputSampleRate; // ms

	// determine conversion ratio:
	Fraction fraction = getFractionFromSamplerates(ci.inputSampleRate, ci.outputSampleRate);

	// set buffer sizes:
	auto inputChannelBufferSize = static_cast<size_t>(BUFFERSIZE);
    auto inputBlockSize = static_cast<size_t>(BUFFERSIZE * nChannels);
	auto outputChannelBufferSize = static_cast<size_t>(1 + std::ceil(BUFFERSIZE * static_cast<double>(fraction.numerator) / static_cast<double>(fraction.denominator)));
	auto outputBlockSize = static_cast<size_t>(nChannels * (1 + outputChannelBufferSize));

	// allocate buffers:
	std::vector<FloatType> inputBlock(inputBlockSize, 0);		// input buffer for storing interleaved samples from input file
	std::vector<FloatType> outputBlock(outputBlockSize, 0);		// output buffer for storing interleaved samples to be saved to output file
	std::vector<std::vector<FloatType>> inputChannelBuffers;	// input buffer for each channel to store deinterleaved samples
	std::vector<std::vector<FloatType>> outputChannelBuffers;	// output buffer for each channel to store converted deinterleaved samples
	for (int n = 0; n < nChannels; n++) {
		inputChannelBuffers.emplace_back(std::vector<FloatType>(inputChannelBufferSize, 0));
		outputChannelBuffers.emplace_back(std::vector<FloatType>(outputChannelBufferSize, 0));
	}

	int inputFileFormat = infile.format();
	if (inputFileFormat != DFF_FORMAT && inputFileFormat != DSF_FORMAT) { // this block only relevant to libsndfile ...
		// detect if input format is a floating-point format:
		bool bFloat = false;
		bool bDouble = false;
		switch (inputFileFormat & SF_FORMAT_SUBMASK) {
		case SF_FORMAT_FLOAT:
			bFloat = true;
			break;
		case SF_FORMAT_DOUBLE:
			bDouble = true;
			break;
		}

		for (auto& subformat : subFormats) { // scan subformats for a match:
			if (subformat.second == (inputFileFormat & SF_FORMAT_SUBMASK)) {
#ifdef COMPILING_ON_ANDROID
		        ANDROID_OUT("input bit format: %s", ANDROID_STDTOC(subformat.first));
#else
				std::cout << "input bit format: " << subformat.first;
#endif
				break;
			}
		}

		if (bFloat)
#ifdef COMPILING_ON_ANDROID
		    ANDROID_OUT(" (float)");
#else
			std::cout << " (float)";
#endif
		if (bDouble)
#ifdef COMPILING_ON_ANDROID
            ANDROID_OUT(" (double precision)");
#else
			std::cout << " (double precision)";
#endif

#ifdef COMPILING_ON_ANDROID
        ANDROID_OUT("");
#else
		std::cout << std::endl;
#endif
	}

#ifdef COMPILING_ON_ANDROID
	ANDROID_OUT("source file channels: %d", nChannels);
    ANDROID_OUT("input sample rate: %d\noutput sample rate: %d", ci.inputSampleRate, ci.outputSampleRate);
#else
	std::cout << "source file channels: " << nChannels << std::endl;
	std::cout << "input sample rate: " << ci.inputSampleRate << "\noutput sample rate: " << ci.outputSampleRate << std::endl;
#endif

	FloatType peakInputSample;
	sf_count_t peakInputPosition = 0LL;
	sf_count_t samplesRead = 0LL;
	sf_count_t totalSamplesRead = 0LL;

	if (ci.bEnablePeakDetection) {
		peakInputSample = 0.0;
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Scanning input file for peaks ...");
#else
		std::cout << "Scanning input file for peaks ...";
#endif

		do {
			samplesRead = infile.read(inputBlock.data(), inputBlockSize);
			for (unsigned int s = 0; s < samplesRead; ++s) { // read all samples, without caring which channel they belong to
				if (std::abs(inputBlock[s]) > peakInputSample) {
					peakInputSample = std::abs(inputBlock[s]);
					peakInputPosition = totalSamplesRead + s;
				}
			}
			totalSamplesRead += samplesRead;
		} while (samplesRead > 0);

#ifdef COMPILING_ON_ANDROID
        ANDROID_OUT("Done");
        ANDROID_OUT("Peak input sample: %G (%G dBFS) at ", peakInputSample, 20 * log10(peakInputSample));
#else
		std::cout << "Done\n";
		std::cout << "Peak input sample: " << std::fixed << peakInputSample << " (" << 20 * log10(peakInputSample) << " dBFS) at ";
#endif
		printSamplePosAsTime(peakInputPosition, static_cast<unsigned int>(ci.inputSampleRate)); // using unsigned int for type int
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("");
#else
		std::cout << std::endl;
#endif
		infile.seek(0, SEEK_SET); // rewind back to start of file
	}

	else { // no peak detection
		peakInputSample = ci.bNormalize ?
			0.5  /* ... a guess, since we haven't actually measured the peak (in the case of DSD, it is a good guess.) */ :
			1.0;
	}

	if (ci.bNormalize) { // echo Normalization settings to user
		auto prec = std::cout.precision();
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Normalizing to %G", ci.limit);
#else
		std::cout << "Normalizing to " << std::setprecision(2) << ci.limit << std::endl;
#endif
		std::cout.precision(prec);
	}

	// echo filter settings to user:
	double targetNyquist = std::min(ci.inputSampleRate, ci.outputSampleRate) / 2.0;
	double ft = (ci.lpfCutoff / 100.0) * targetNyquist;
	auto prec = std::cout.precision();
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("LPF transition frequency: %G Hz (%G %%)", ft, 100 * ft / targetNyquist);
#else
	std::cout << "LPF transition frequency: " << std::fixed << std::setprecision(2) << ft << " Hz (" << 100 * ft / targetNyquist << " %)" << std::endl;
#endif
	std::cout.precision(prec);
	if (ci.bMinPhase) {
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Using Minimum-Phase LPF");
#else
		std::cout << "Using Minimum-Phase LPF" << std::endl;
#endif
	}

	// echo conversion ratio to user:
	FloatType resamplingFactor = static_cast<FloatType>(ci.outputSampleRate) / ci.inputSampleRate;
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("Conversion ratio: %G (%d:%d)", resamplingFactor, fraction.numerator, fraction.denominator);
#else
	std::cout << "Conversion ratio: " << resamplingFactor
		<< " (" << fraction.numerator << ":" << fraction.denominator << ")" << std::endl;
#endif

	// if the outputFormat is zero, it means "No change to file format"
	// if output file format has changed, use outputFormat. Otherwise, use same format as infile:
	int outputFileFormat = ci.outputFormat ? ci.outputFormat : inputFileFormat;

	// if the minor (sub) format of outputFileFormat is not set, attempt to use minor format of input file (as a last resort)
	if ((outputFileFormat & SF_FORMAT_SUBMASK) == 0) {
		outputFileFormat |= (inputFileFormat & SF_FORMAT_SUBMASK); // may not be valid subformat for new file format.
	}

	// for wav files, determine whether to switch to rf64 mode:
	if (((outputFileFormat & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV) ||
		((outputFileFormat & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAVEX)) {
		if (ci.bRf64 ||
			checkWarnOutputSize(inputSampleCount, getSfBytesPerSample(outputFileFormat), fraction.numerator, fraction.denominator)) {
#ifdef COMPILING_ON_ANDROID
		    ANDROID_OUT("Switching to rf64 format !");
#else
			std::cout << "Switching to rf64 format !" << std::endl;
#endif
			outputFileFormat &= ~SF_FORMAT_TYPEMASK; // clear file type
			outputFileFormat |= SF_FORMAT_RF64;
		}
	}

	// note: libsndfile has an rf64 auto-downgrade mode:
	// http://www.mega-nerd.com/libsndfile/command.html#SFC_RF64_AUTO_DOWNGRADE
	// However, rf64 auto-downgrade is more appropriate for recording applications
	// (where the final file size cannot be known until the recording has stopped)
	// In the case of sample-rate conversions, the output file size (and therefore the decision to promote to rf64)
	// can be determined at the outset.

    // Determine the value of outputSignalBits, based on outputFileFormat.
	// outputSignalsBits is used to set the level of the LSB for dithering
	int outputSignalBits;
	switch (outputFileFormat & SF_FORMAT_SUBMASK) {
	case SF_FORMAT_PCM_24:
		outputSignalBits = 24;
		break;
	case SF_FORMAT_PCM_S8:
	case SF_FORMAT_PCM_U8:
		outputSignalBits = 8;
		break;
    case SF_FORMAT_DOUBLE:
        outputSignalBits = 53;
        break;
    case SF_FORMAT_FLOAT:
        outputSignalBits = 21;
        break;
	default:
		outputSignalBits = 16;
	}

	if(ci.quantize) {
	    outputSignalBits = std::max(1, std::min(ci.quantizeBits, outputSignalBits));
	}

	// confirm dithering options for user:
	if (ci.bDither) {
		auto prec = std::cout.precision();
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Generating %G bits of %s dither for %d-bit output format", ci.ditherAmount, ditherProfileList[ci.ditherProfileID].name, outputSignalBits);
#else
		std::cout << "Generating " << std::setprecision(2) << ci.ditherAmount << " bits of " << ditherProfileList[ci.ditherProfileID].name << " dither for " << outputSignalBits << "-bit output format";
#endif
		std::cout.precision(prec);
		if (ci.bAutoBlankingEnabled)
#ifdef COMPILING_ON_ANDROID
		    ANDROID_OUT(", with auto-blanking");
#else
			std::cout << ", with auto-blanking";
#endif
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("");
#else
		std::cout << std::endl;
#endif
	}

	// make a vector of ditherers (one ditherer for each channel):
	std::vector<Ditherer<FloatType>> ditherers;
    auto seed = static_cast<int>(ci.bUseSeed ? ci.seed : time(nullptr));

	for (int n = 0; n < nChannels; n++) {
		// to-do: explore other seed-generation options (remote possibility of overlap)
		// maybe use a single global RNG ?
		// or use discard/jump-ahead ... to ensure parallel streams are sufficiently "far away" from each other ?
		ditherers.emplace_back(outputSignalBits, ci.ditherAmount, ci.bAutoBlankingEnabled, n + seed, static_cast<DitherProfileID>(ci.ditherProfileID));
	}

	// make a vector of Resamplers
	std::vector<Converter<FloatType>> converters;
	for (int n = 0; n < nChannels; n++) {
		converters.emplace_back(ci);
	}

	// Calculate initial gain:
	FloatType gain = ci.gain * converters[0].getGain() *
		(ci.bNormalize ? fraction.numerator * (ci.limit / peakInputSample) : fraction.numerator * ci.limit );

	// todo: more testing with very low bit depths (eg 4 bits)
	if (ci.bDither) { // allow headroom for dithering:
		FloatType ditherCompensation =
			(pow(2, outputSignalBits - 1) - pow(2, ci.ditherAmount - 1)) / pow(2, outputSignalBits - 1); // eg 32767/32768 = 0.999969 (-0.00027 dB)
		gain *= ditherCompensation;
	}

    int groupDelay = static_cast<int>(converters[0].getGroupDelay());

	FloatType peakOutputSample;
	bool bClippingDetected;
	RaiiTimer timer(inputDuration);

	int clippingProtectionAttempts = 0;

	do { // clipping detection loop (repeats if clipping detected AND not using a temp file)

		infile.seek(0, SEEK_SET);
		peakInputSample = 0.0;
		bClippingDetected = false;
		std::unique_ptr<SndfileHandle> outFile;
		std::unique_ptr<CsvFile> csvFile;

		if (ci.csvOutput) { // csv output
			csvFile.reset(new CsvFile(ci.outputFilename));
			csvFile->setNumChannels(nChannels);

			// defaults
			csvFile->setNumericBase(Decimal);
			csvFile->setIntegerWriteScalingStyle(ci.integerWriteScalingStyle);
			csvFile->setSignedness(Signed);
			csvFile->setNumericFormat(Integer);

			if (ci.outBitFormat.empty()) { // default = 16-bit, unsigned, integer (decimal)
				csvFile->setNumBits(16);
			}
			else {
				std::regex rgx("([us]?)(\\d+)([fiox]?)"); // [u|s]<numBits>[f|i|o|x]
                std::smatch matchResults;
                std::regex_search(ci.outBitFormat, matchResults, rgx);
				int numBits = 16;

				if (matchResults.length() >= 1 && matchResults[1].compare("u") == 0) {
					csvFile->setSignedness(Unsigned);
				}

				if (matchResults.length() >= 2 && std::stoi(matchResults[2]) != 0) {
					numBits = std::min(std::max(1, std::stoi(matchResults[2])), 64); // 1-64 bits
				}

				if (matchResults.length() >= 3 && !matchResults[3].str().empty()) {
					if (matchResults[3].compare("f") == 0) {
						csvFile->setNumericFormat(FloatingPoint);
					}
					else if (matchResults[3].compare("o") == 0) {
						csvFile->setNumericBase(Octal);
					}
					else if (matchResults[3].compare("x") == 0) {
						csvFile->setNumericBase(Hexadecimal);
					}
				}

				csvFile->setNumBits(numBits);

                // todo: precision, other params
            }
		}

		else { // libSndFile output

			try {

				// output file may need to be overwriten on subsequent passes,
				// and the only way to close the file is to destroy the SndfileHandle.

				outFile.reset(new SndfileHandle(ci.outputFilename, SFM_WRITE, outputFileFormat, nChannels, ci.outputSampleRate));

				if (int e = outFile->error()) {
#ifdef COMPILING_ON_ANDROID
            		ANDROID_ERR("Error: Couldn't Open Output File (%s)", sf_error_number(e));
#else
					std::cerr << "Error: Couldn't Open Output File (" << sf_error_number(e) << ")" << std::endl;
#endif
					return false;
				}

				if (ci.bNoPeakChunk) {
					outFile->command(SFC_SET_ADD_PEAK_CHUNK, nullptr, SF_FALSE);
				}

				if (ci.bWriteMetaData) {
					if (!setMetaData(m, *outFile)) {
#ifdef COMPILING_ON_ANDROID
                		ANDROID_OUT("Warning: problem writing metadata to output file ( %s )", outFile->strError());
#else
						std::cout << "Warning: problem writing metadata to output file ( " << outFile->strError() << " )" << std::endl;
#endif
					}
				}

				// if the minor (sub) format of outputFileFormat is flac, and user has requested a specific compression level, set compression level:
				if (((outputFileFormat & SF_FORMAT_FLAC) == SF_FORMAT_FLAC) && ci.bSetFlacCompression) {
#ifdef COMPILING_ON_ANDROID
				    ANDROID_OUT("setting flac compression level to %d", ci.flacCompressionLevel);
#else
					std::cout << "setting flac compression level to " << ci.flacCompressionLevel << std::endl;
#endif
					double cl = ci.flacCompressionLevel / 8.0; // there are 9 flac compression levels from 0-8. Normalize to 0-1.0
					outFile->command(SFC_SET_COMPRESSION_LEVEL, &cl, sizeof(cl));
				}

				// if the minor (sub) format of outputFileFormat is vorbis, and user has requested a specific quality level, set quality level:
				if (((outputFileFormat & SF_FORMAT_VORBIS) == SF_FORMAT_VORBIS) && ci.bSetVorbisQuality) {

					auto prec = std::cout.precision();
                    std::cout.precision(1);
#ifdef COMPILING_ON_ANDROID
            		ANDROID_OUT("setting vorbis quality level to %G", ci.vorbisQuality);
#else
					std::cout << "setting vorbis quality level to " << ci.vorbisQuality << std::endl;
#endif
					std::cout.precision(prec);

					double cl = (1.0 - ci.vorbisQuality) / 11.0; // Normalize from (-1 to 10), to (1.0 to 0) ... why is it backwards ?
					outFile->command(SFC_SET_COMPRESSION_LEVEL, &cl, sizeof(cl));
				}
			}

			catch (std::exception& e) {
#ifdef COMPILING_ON_ANDROID
        		ANDROID_ERR("Error: Couldn't Open Output File %s", e.what());
#else
				std::cerr << "Error: Couldn't Open Output File " << e.what() << std::endl;
#endif
				return false;
			}
		}

		// conditionally open a temp file:
		if (ci.bTmpFile) {
            tmpSndfileHandle = getTempFile<FloatType>(inputFileFormat, nChannels, ci, tmpFilename);
            if(tmpSndfileHandle == nullptr) {
                ci.bTmpFile = false;
            }
		} // ends opening of temp file

		// echo conversion mode to user (multi-stage/single-stage, multi-threaded/single-threaded)
		std::string stageness(ci.bMultiStage ? "multi-stage" : "single-stage");
		std::string threadedness(ci.bMultiThreaded ? ", multi-threaded" : "");
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Converting (%s%s) ...", ANDROID_STDTOC(stageness), ANDROID_STDTOC(threadedness));
#else
		std::cout << "Converting (" << stageness << threadedness << ") ..." << std::endl;
#endif

		peakOutputSample = 0.0;
		totalSamplesRead = 0;
		sf_count_t incrementalProgressThreshold = inputSampleCount / 10;
		sf_count_t nextProgressThreshold = incrementalProgressThreshold;

		int outStartOffset = std::min(groupDelay * nChannels, static_cast<int>(outputBlockSize) - nChannels);

		do { // central conversion loop (the heart of the matter ...)

			// Grab a block of interleaved samples from file:
			samplesRead = infile.read(inputBlock.data(), inputBlockSize);
			totalSamplesRead += samplesRead;

			// de-interleave into channel buffers
			size_t i = 0;
			for (size_t s = 0 ; s < samplesRead; s += nChannels) {
				for (int ch = 0 ; ch < nChannels; ++ch) {
					inputChannelBuffers[ch][i] = inputBlock[s+ch];
				}
				++i;
			}

			struct Result {
				size_t outBlockindex;
				FloatType peak;
			};

			std::vector<std::future<Result>> results(nChannels);
			ctpl::thread_pool threadPool(nChannels);
            size_t outputBlockIndex = 0;

			for (int ch = 0; ch < nChannels; ++ch) { // run convert stage for each channel (concurrently)

				auto kernel = [&, ch](int x = 0) {
					FloatType* iBuf = inputChannelBuffers[ch].data();
					FloatType* oBuf = outputChannelBuffers[ch].data();
					size_t o = 0;
					FloatType localPeak = 0.0;
					size_t localOutputBlockIndex = 0;
					converters[ch].convert(oBuf, o, iBuf, i);
					for (size_t f = 0; f < o; ++f) {
						// note: disable dither for temp files (dithering to be done in post)
						FloatType outputSample = (ci.bDither && !ci.bTmpFile) ? ditherers[ch].dither(gain * oBuf[f]) : gain * oBuf[f]; // gain, dither
						localPeak = std::max(localPeak, std::abs(outputSample)); // peak
						outputBlock[localOutputBlockIndex + ch] = outputSample; // interleave
						localOutputBlockIndex += nChannels;
					}
					Result res;
					res.outBlockindex = localOutputBlockIndex;
					res.peak = localPeak;
					return res;
				};

				if (multiThreaded) {
					results[ch] = threadPool.push(kernel);
				}
				else {
					Result res = kernel();
					peakOutputSample = std::max(peakOutputSample, res.peak);
					outputBlockIndex = res.outBlockindex;
				}
			}

			if (multiThreaded) { // collect results:
				for (int ch = 0; ch < nChannels; ++ch) {
					Result res = results[ch].get();
					peakOutputSample = std::max(peakOutputSample, res.peak);
					outputBlockIndex = res.outBlockindex;
				}
			}

			// write to either temp file or outfile (with Group Delay Compensation):
			if (ci.bTmpFile) {
				tmpSndfileHandle->write(outputBlock.data() + outStartOffset, outputBlockIndex - outStartOffset);
			}
			else {
				if (ci.csvOutput) {
					csvFile->write(outputBlock.data() + outStartOffset, outputBlockIndex - outStartOffset);
				}
				else {
					outFile->write(outputBlock.data() + outStartOffset, outputBlockIndex - outStartOffset);
				}
			}
			outStartOffset = 0; // reset after first use

			// conditionally send progress update:
			if (totalSamplesRead > nextProgressThreshold) {
				int progressPercentage = std::min(static_cast<int>(99), static_cast<int>(100 * totalSamplesRead / inputSampleCount));
#ifdef COMPILING_ON_ANDROID
        		ANDROID_OUT("%d%%", progressPercentage); // logcat cannot handle backspace '\b' formatter
#else
				std::cout << progressPercentage << "%\b\b\b" << std::flush;
#endif
				nextProgressThreshold += incrementalProgressThreshold;
			}

		} while (samplesRead > 0); // ends central conversion loop

		if (ci.bTmpFile) {
			gain = 1.0; // output file must start with unity gain relative to temp file
		} else {
			// notify user:
#ifdef COMPILING_ON_ANDROID
    		ANDROID_OUT("Done");
#else
			std::cout << "Done" << std::endl;
#endif
			auto prec = std::cout.precision();
#ifdef COMPILING_ON_ANDROID
		    ANDROID_OUT("Peak output sample: %G (%G  dBFS)", peakOutputSample, 20 * log10(peakOutputSample));
#else
			std::cout << "Peak output sample: " << std::setprecision(6) << peakOutputSample << " (" << 20 * log10(peakOutputSample) << " dBFS)" << std::endl;
#endif
			std::cout.precision(prec);
		}

		do {
			// test for clipping:
			if (!ci.disableClippingProtection && peakOutputSample > ci.limit) {

#ifdef COMPILING_ON_ANDROID
        		ANDROID_OUT("Clipping detected !");
#else
				std::cout << "\nClipping detected !" << std::endl;
#endif

				// calculate gain adjustment
				FloatType gainAdjustment = static_cast<FloatType>(clippingTrim) * ci.limit / peakOutputSample;
				gain *= gainAdjustment;

				// echo gain adjustment to user - use slightly differnt message if using temp file:
				if (ci.bTmpFile) {
#ifdef COMPILING_ON_ANDROID
		            ANDROID_OUT("Adjusting gain by %G dB", 20 * log10(gainAdjustment));
#else
					std::cout << "Adjusting gain by " << 20 * log10(gainAdjustment) << " dB" << std::endl;
#endif
				}
				else {
#ifdef COMPILING_ON_ANDROID
            		ANDROID_OUT("Re-doing with %G dB gain adjustment", 20 * log10(gainAdjustment));
#else
					std::cout << "Re-doing with " << 20 * log10(gainAdjustment) << " dB gain adjustment" << std::endl;
#endif
				}

				// reset the ditherers
				if (ci.bDither) {
					for (auto &ditherer : ditherers) {
						ditherer.adjustGain(gainAdjustment);
						ditherer.reset();
					}
				}

				// reset the converters
				for (auto &converter : converters) {
					converter.reset();
				}

			} // ends test for clipping

			// if using temp file, write to outFile
			if (ci.bTmpFile) {

#ifdef COMPILING_ON_ANDROID
		        ANDROID_OUT("Writing to output file ...");
#else
				std::cout << "Writing to output file ...\n";
#endif
				std::vector<FloatType> outBuf(inputBlockSize, 0);
				peakOutputSample = 0.0;
				totalSamplesRead = 0;
				incrementalProgressThreshold = inputSampleCount / 10;
				nextProgressThreshold = incrementalProgressThreshold;

				tmpSndfileHandle->seek(0, SEEK_SET);
				if (!ci.csvOutput) {
					outFile->seek(0, SEEK_SET);
				}

				do { // Grab a block of interleaved samples from temp file:
					samplesRead = tmpSndfileHandle->read(inputBlock.data(), inputBlockSize);
					totalSamplesRead += samplesRead;

					// de-interleave into channels, apply gain, add dither, and save to output buffer
					size_t i = 0;
					for (size_t s = 0; s < samplesRead; s += nChannels) {
						for (int ch = 0; ch < nChannels; ++ch) {
							FloatType smpl = ci.bDither ? ditherers[ch].dither(gain * inputBlock[i]) :
								gain * inputBlock[i];
							peakOutputSample = std::max(std::abs(smpl), peakOutputSample);
							outBuf[i++] = smpl;
						}
					}

					// write output buffer to outfile
					if (ci.csvOutput) {
						csvFile->write(outBuf.data(), i);
					}
					else {
						outFile->write(outBuf.data(), i);
					}

					// conditionally send progress update:
					if (totalSamplesRead > nextProgressThreshold) {
						int progressPercentage = std::min(static_cast<int>(99),
							static_cast<int>(100 * totalSamplesRead / inputSampleCount));
#ifdef COMPILING_ON_ANDROID
                        ANDROID_OUT("%d%%", progressPercentage); // logcat cannot handle backspace '\b' formatter
#else
						std::cout << progressPercentage << "%\b\b\b" << std::flush;
#endif
						nextProgressThreshold += incrementalProgressThreshold;
					}

				} while (samplesRead > 0);

#ifdef COMPILING_ON_ANDROID
		        ANDROID_OUT("Done");
#else
				std::cout << "Done" << std::endl;
#endif
				auto prec = std::cout.precision();
#ifdef COMPILING_ON_ANDROID
		        ANDROID_OUT("Peak output sample: %G (%G dBFS)", peakOutputSample, 20 * log10(peakOutputSample));
#else
				std::cout << "Peak output sample: " << std::setprecision(6) << peakOutputSample << " (" << 20 * log10(peakOutputSample) << " dBFS)" << std::endl;
#endif
				std::cout.precision(prec);

			} // ends if (ci.bTmpFile)

			bClippingDetected = peakOutputSample > ci.limit;
			if (bClippingDetected)
				clippingProtectionAttempts++;

			// explanation of 'while' loops:
			// 1. when clipping is detected and temp file is in use, go back to re-adjusting gain, resetting ditherers etc and repeat
			// 2. when clipping is detected and temp file NOT used, go all the way back to reading the input file, and running the whole conversion again
			// (This whole control structure might be better served with good old gotos ...)

		} while (ci.bTmpFile && !ci.disableClippingProtection && bClippingDetected && clippingProtectionAttempts < maxClippingProtectionAttempts); // if using temp file, do another round if clipping detected
	} while (!ci.bTmpFile && !ci.disableClippingProtection && bClippingDetected && clippingProtectionAttempts < maxClippingProtectionAttempts); // if NOT using temp file, do another round if clipping detected

	// clean-up temp file:
	delete tmpSndfileHandle; // dealllocate SndFileHandle

	#if defined (TEMPFILE_OPEN_METHOD_STD_TMPNAM) || defined (TEMPFILE_OPEN_METHOD_WINAPI)
		std::remove(tmpFilename.c_str()); // actually remove the temp file from disk
	#endif

	return true;
} // ends convert()

// getTempFile() : opens a temp file (wav/rf64 file in floating-point format).
// Double- or single- precision is determined by FloatType.
// Dynamically allocates a SndfileHandle.
// Returns SndfileHandle pointer, which is the caller's responsibility to delete.
// Returns nullptr if unsuccessful.

template<typename FloatType>
SndfileHandle* getTempFile(int inputFileFormat, int nChannels, const ConversionInfo& ci, std::string& tmpFilename) {

    SndfileHandle* tmpSndfileHandle = nullptr;
    bool tmpFileError;
    int outputFileFormat = ci.outputFormat ? ci.outputFormat : inputFileFormat;

    // set major format of temp file (inherit rf64-ness from output file):
    int tmpFileFormat = (outputFileFormat & SF_FORMAT_RF64) ? SF_FORMAT_RF64 : SF_FORMAT_WAV;

    // set appropriate floating-point subformat:
    tmpFileFormat |= (sizeof(FloatType) == 8) ? SF_FORMAT_DOUBLE : SF_FORMAT_FLOAT;

#if defined (TEMPFILE_OPEN_METHOD_WINAPI)
    TCHAR _tmpFilename[MAX_PATH];
    TCHAR _tmpPathname[MAX_PATH];
    tmpFileError = true;
	DWORD pathLen;

	if (!ci.tmpDir.empty()) {
		pathLen = ci.tmpDir.length();
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> widener;
		wcscpy_s(_tmpPathname, MAX_PATH, widener.from_bytes(ci.tmpDir).c_str());
	}
	else {
		pathLen = GetTempPath(MAX_PATH, _tmpPathname);
	}

    if (pathLen > MAX_PATH || pathLen == 0)
#ifdef COMPILING_ON_ANDROID
		ANDROID_ERR("Error: Could not determine temp path for temp file");
#else
        std::cerr << "Error: Could not determine temp path for temp file" << std::endl;
#endif
    else {
        if (GetTempFileName(_tmpPathname, TEXT("ReS"), 0, _tmpFilename) == 0)
#ifdef COMPILING_ON_ANDROID
		    ANDROID_ERR("Error: Couldn't generate temp file name");
#else
            std::cerr << "Error: Couldn't generate temp file name" << std::endl;
#endif
        else {
            tmpFileError = false;
            std::wstring_convert<std::codecvt_utf8<wchar_t>> wchar2utf8;
            tmpFilename = wchar2utf8.to_bytes(_tmpFilename);
            if(ci.bShowTempFile)
#ifdef COMPILING_ON_ANDROID
		        ANDROID_OUT("Temp Filename: %s", ANDROID_STDTOC(tmpFilename));
#else
                std::cout << "Temp Filename: " <<  tmpFilename << std::endl;
#endif
            tmpSndfileHandle = new SndfileHandle(tmpFilename, SFM_RDWR, tmpFileFormat, nChannels, ci.outputSampleRate); // open using filename
        }
    }

#elif defined (TEMPFILE_OPEN_METHOD_STD_TMPFILE)
    FILE* f = std::tmpfile();
    tmpFileError = (f == NULL);
    if (!tmpFileError) {
        tmpSndfileHandle = new SndfileHandle(fileno(f), true, SFM_RDWR, tmpFileFormat, nChannels, ci.outputSampleRate); // open using file descriptor
    } else {
#ifdef COMPILING_ON_ANDROID
		ANDROID_ERR("std::tmpfile() failed");
#else
        std::cerr << "std::tmpfile() failed" << std::endl;
#endif
    }

#elif defined (TEMPFILE_OPEN_METHOD_MKSTEMP)
    char templ[] = "ReSamplerXXXXXX";
    int fd = mkstemp(templ);
    tmpFileError = (fd == -1);
    if(!tmpFileError) {
        if(ci.bShowTempFile) printf("temp file: %s\n", templ);
        tmpSndfileHandle = new SndfileHandle(fd, true, SFM_RDWR, tmpFileFormat, nChannels, ci.outputSampleRate); // open using file descriptor
    } else {
#ifdef COMPILING_ON_ANDROID
		ANDROID_ERR("std::mkstemp() failed");
#else
        std::cerr << "std::mkstemp() failed" << std::endl;
#endif
    }

#else
    // tmpnam() method
    tmpFileError = false;
    tmpFilename = std::string(std::string(std::tmpnam(nullptr)) + ".wav");
    if (ci.bShowTempFile)
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Temp Filename: %s", ANDROID_STDTOC(tmpFilename));
#else
        std::cout << "Temp Filename: " << tmpFilename << std::endl;
#endif
    tmpSndfileHandle = new SndfileHandle(tmpFilename, SFM_RDWR, tmpFileFormat, nChannels, ci.outputSampleRate); // open using filename

#endif

    int e = 0;
    if (tmpFileError || tmpSndfileHandle == nullptr || (e = tmpSndfileHandle->error())){
#ifdef COMPILING_ON_ANDROID
		ANDROID_ERR("Error: Couldn't Open Temporary File (%s)\nDisabling temp file mode.", sf_error_number(e));
#else
        std::cerr << "Error: Couldn't Open Temporary File (" << sf_error_number(e) << ")\n";
        std::cout << "Disabling temp file mode." << std::endl;
#endif
        tmpSndfileHandle = nullptr;
    } else {
        // disable floating-point normalisation (important - we want to record/recover floating point values exactly)
        if (sizeof(FloatType) == 8) {
            tmpSndfileHandle->command(SFC_SET_NORM_DOUBLE, NULL,
                                      SF_FALSE); // http://www.mega-nerd.com/libsndfile/command.html#SFC_SET_NORM_DOUBLE
        } else {
            tmpSndfileHandle->command(SFC_SET_NORM_FLOAT, NULL, SF_FALSE);
        }
    }

    return tmpSndfileHandle;
}

// retrieve metadata using libsndfile API :
bool getMetaData(MetaData& metadata, SndfileHandle& infile) {
	const char* empty = "";
	const char* str;

	metadata.title.assign((str = infile.getString(SF_STR_TITLE)) ? str : empty);
	metadata.copyright.assign((str = infile.getString(SF_STR_COPYRIGHT)) ? str : empty);
	metadata.software.assign((str = infile.getString(SF_STR_SOFTWARE)) ? str : empty);
	metadata.artist.assign((str = infile.getString(SF_STR_ARTIST)) ? str : empty);
	metadata.comment.assign((str = infile.getString(SF_STR_COMMENT)) ? str : empty);
	metadata.date.assign((str = infile.getString(SF_STR_DATE)) ? str : empty);
	metadata.album.assign((str = infile.getString(SF_STR_ALBUM)) ? str : empty);
	metadata.license.assign((str = infile.getString(SF_STR_LICENSE)) ? str : empty);
	metadata.trackNumber.assign((str = infile.getString(SF_STR_TRACKNUMBER)) ? str : empty);
	metadata.genre.assign((str = infile.getString(SF_STR_GENRE)) ? str : empty);

	// retrieve Broadcast Extension (bext) chunk, if it exists:
	metadata.has_bext_fields = (infile.command(SFC_GET_BROADCAST_INFO, (void*)&metadata.broadcastInfo, sizeof(SF_BROADCAST_INFO)) == SF_TRUE);

	if (metadata.has_bext_fields) {
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Input file contains a Broadcast Extension (bext) chunk");
#else
		std::cout << "Input file contains a Broadcast Extension (bext) chunk" << std::endl;
#endif
	}

	// retrieve cart chunk, if it exists:
	metadata.has_cart_chunk = (infile.command(SFC_GET_CART_INFO, (void*)&metadata.cartInfo, sizeof(LargeSFCartInfo)) == SF_TRUE);

	if (metadata.has_cart_chunk) {
		// Note: size of CART chunk is variable, depending on size of last field (tag_text[])
		if (metadata.cartInfo.tag_text_size > MAX_CART_TAG_TEXT_SIZE) {
			metadata.cartInfo.tag_text_size = MAX_CART_TAG_TEXT_SIZE; // apply hard limit on number of characters (spec says unlimited ...)
		}
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Input file contains a cart chunk");
#else
		std::cout << "Input file contains a cart chunk" << std::endl;
#endif
	}
	return true;
}

// set metadata using libsndfile API :
bool setMetaData(const MetaData& metadata, SndfileHandle& outfile) {

#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("Writing Metadata");
#else
	std::cout << "Writing Metadata" << std::endl;
#endif
	if (!metadata.title.empty()) outfile.setString(SF_STR_TITLE, metadata.title.c_str());
	if (!metadata.copyright.empty()) outfile.setString(SF_STR_COPYRIGHT, metadata.copyright.c_str());
	if (!metadata.software.empty()) outfile.setString(SF_STR_SOFTWARE, metadata.software.c_str());
	if (!metadata.artist.empty()) outfile.setString(SF_STR_ARTIST, metadata.artist.c_str());
	if (!metadata.comment.empty()) outfile.setString(SF_STR_COMMENT, metadata.comment.c_str());
	if (!metadata.date.empty()) outfile.setString(SF_STR_DATE, metadata.date.c_str());
	if (!metadata.album.empty()) outfile.setString(SF_STR_ALBUM, metadata.album.c_str());
	if (!metadata.license.empty()) outfile.setString(SF_STR_LICENSE, metadata.license.c_str());
	if (!metadata.trackNumber.empty()) outfile.setString(SF_STR_TRACKNUMBER, metadata.trackNumber.c_str());
	if (!metadata.genre.empty()) outfile.setString(SF_STR_GENRE, metadata.genre.c_str());

	if (((outfile.format() &  SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV) ||
		((outfile.format() &  SF_FORMAT_TYPEMASK) == SF_FORMAT_WAVEX) ||
		((outfile.format() &  SF_FORMAT_TYPEMASK) == SF_FORMAT_RF64)) { /* some sort of wav file */

		// attempt to write bext / cart chunks:
		if (metadata.has_bext_fields) {
			outfile.command(SFC_SET_BROADCAST_INFO, (void*)&metadata.broadcastInfo, sizeof(SF_BROADCAST_INFO));
		}

		if (metadata.has_cart_chunk) {
			outfile.command(SFC_SET_CART_INFO,
				(void*)&metadata.cartInfo,
				sizeof(metadata.cartInfo) - MAX_CART_TAG_TEXT_SIZE + metadata.cartInfo.tag_text_size // (size of cartInfo WITHOUT tag text) + (actual size of tag text)
			);
		}
	}

	return (outfile.error() == 0);
}

bool testSetMetaData(SndfileHandle& outfile) {
	MetaData m;
    memset(&m, 0, sizeof(m));
	m.title.assign("test title");
	m.copyright.assign("test copyright");
	m.software.assign("test software");
	m.artist.assign("test artist");
	m.comment.assign("test comment");
	m.date.assign("test date");
	m.album.assign("test album");
	m.license.assign("test license");
	m.trackNumber.assign("test track number");
	m.genre.assign("test genre");
	return setMetaData(m, outfile);
}

int getDefaultNoiseShape(int sampleRate) {
	if (sampleRate <= 44100) {
		return DitherProfileID::standard;
	}
	else if (sampleRate <= 48000) {
		return DitherProfileID::standard;
	}
	else {
		return DitherProfileID::flat_f;
	}
}

void showDitherProfiles() {
	for (int d = DitherProfileID::flat; d != DitherProfileID::end; ++d) {
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("%d:%s", ditherProfileList[d].id, ditherProfileList[d].name);
#else
		std::cout << ditherProfileList[d].id << " : " << ditherProfileList[d].name << std::endl;
#endif
	}
}

int getSfBytesPerSample(int format) {
	int subformat = format & SF_FORMAT_SUBMASK;
	switch (subformat) {
	case SF_FORMAT_PCM_S8:
		return 1;
	case SF_FORMAT_PCM_16:
		return 2;
	case SF_FORMAT_PCM_24:
		return 3;
	case SF_FORMAT_PCM_32:
		return 4;
	case SF_FORMAT_PCM_U8:
		return 1;
	case SF_FORMAT_FLOAT:
		return 4;
	case SF_FORMAT_DOUBLE:
		return 8;
	default:
		return 2; // for safety
	}
}

bool checkWarnOutputSize(sf_count_t inputSamples, int bytesPerSample, int numerator, int denominator)
{
	sf_count_t outputDataSize = inputSamples * bytesPerSample * numerator / denominator;

	const sf_count_t limit4G = 1ULL << 32;
	if (outputDataSize >= limit4G) {
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Warning: output file ( %s bytes of data ) will exceed 4GB limit", ANDROID_STDTOC(fmtNumberWithCommas(outputDataSize)));
#else
		std::cout << "Warning: output file ( " << fmtNumberWithCommas(outputDataSize) << " bytes of data ) will exceed 4GB limit"  << std::endl;
#endif
		return true;
	}
	return false;
}

template<typename IntType>
std::string fmtNumberWithCommas(IntType n) {
	std::string s = std::to_string(n);
	int64_t insertPosition = s.length() - 3;
	while (insertPosition > 0) {
		s.insert(static_cast<size_t>(insertPosition), ",");
		insertPosition -= 3;
	}
	return s;
}

void printSamplePosAsTime(sf_count_t samplePos, unsigned int sampleRate) {
	double seconds = static_cast<double>(samplePos) / sampleRate;
    auto h = static_cast<int>(seconds / 3600);
    auto m = static_cast<int>((seconds - (h * 3600)) / 60);
	double s = seconds - (h * 3600) - (m * 60);
	std::ios::fmtflags f(std::cout.flags());
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("%d:%d:%G", h, m, s);
#else
	std::cout << std::setprecision(0) << h << ":" << m << ":" << std::setprecision(6) << s;
#endif
	std::cout.flags(f);
}

bool testSetMetaData(DsfFile& outfile) {
	// stub - to-do
	return true;
}

bool testSetMetaData(DffFile& outfile) {
	// stub - to-do
	return true;
}

bool getMetaData(MetaData& metadata, const DffFile& f) {
	// stub - to-do
	return true;
}

bool getMetaData(MetaData& metadata, const DsfFile& f) {
	// stub - to-do
	return true;
}

#ifndef FIR_QUAD_PRECISION

void generateExpSweep(const std::string& filename, int sampleRate, int format, double duration, int nOctaves, double amplitude_dB) {
	int pow2P = 1 << nOctaves;
	int pow2P1 = 1 << (nOctaves + 1);
	double amplitude = pow(10.0, (amplitude_dB / 20.0));
	double M = pow2P1 * nOctaves * M_LN2;
	int N = lround((duration * sampleRate) / M) * M; // N must be integer multiple of M
	double y = log(pow2P);
	double C = (N * M_PI / pow2P) / y;
	double TWOPI = 2.0 * M_PI;

	SndfileHandle outFile(filename, SFM_WRITE, format, 1, sampleRate);
	std::vector<double> signal(N, 0.0);

	for(int n = 0; n < N; n++) {
		signal[n] = amplitude * sin(fmod(C * exp(y * n / N), TWOPI));
	}

	outFile.write(signal.data(), N);
}

#else // QUAD PRECISION VERSION

void generateExpSweep(const std::string& filename, int sampleRate, int format, double duration, int nOctaves, double amplitude_dB) {

	int pow2P = 1 << nOctaves;
	int pow2P1 = 1 << (nOctaves + 1);
	__float128 amplitude = pow(10.0Q, (amplitude_dB / 20.0Q));
	__float128 M = pow2P1 * nOctaves * M_LN2q;
	int N = lroundq((duration * sampleRate) / M) * M; // N must be integer multiple of M
	__float128 y = logq(pow2P);
	__float128 C = (N * M_PIq / pow2P) / y;
	__float128 TWOPI = 2.0Q * M_PIq;

	SndfileHandle outFile(filename, SFM_WRITE, format, 1, sampleRate);
	std::vector<double> signal(N, 0.0);

	for (int n = 0; n < N; n++) {
		signal[n] = amplitude * sinq(fmodq(C * expq(y * n / N), TWOPI));
	}

	outFile.write(signal.data(), N);
}

#endif

bool checkSSE2() {
#if defined (_MSC_VER) || defined (__INTEL_COMPILER)
	bool bSSE2ok = false;
	int CPUInfo[4] = { 0,0,0,0 };
	__cpuid(CPUInfo, 0);
	if (CPUInfo[0] != 0) {
		__cpuid(CPUInfo, 1);
		if (CPUInfo[3] & (1 << 26))
			bSSE2ok = true;
	}
	if (bSSE2ok) {
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("CPU supports SSE2 (ok)");
#else
		std::cout << "CPU supports SSE2 (ok)";
#endif
		return true;
	}
	else {
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Your CPU doesn't support SSE2 - please try a non-SSE2 build on this machine");
#else
		std::cout << "Your CPU doesn't support SSE2 - please try a non-SSE2 build on this machine" << std::endl;
#endif
		return false;
	}
#endif // defined (_MSC_VER) || defined (__INTEL_COMPILER)
return true; // todo: fix the check on gcc
}

bool checkAVX() {
#if defined (_MSC_VER) || defined (__INTEL_COMPILER)
	// Verify CPU capabilities:
	bool bAVXok = false;
	int cpuInfo[4] = { 0,0,0,0 };
	__cpuid(cpuInfo, 0);
	if (cpuInfo[0] != 0) {
		__cpuid(cpuInfo, 1);
		if (cpuInfo[2] & (1 << 28)) {
			bAVXok = true; // Note: this test only confirms CPU AVX capability, and does not check OS capability.
						   // to-do: check for AVX2 ...
		}
	}
	if (bAVXok) {
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("CPU supports AVX (ok)");
#else
		std::cout << "CPU supports AVX (ok)";
#endif
		return true;
	}
	else {
#ifdef COMPILING_ON_ANDROID
		ANDROID_OUT("Your CPU doesn't support AVX - please try a non-AVX build on this machine");
#else
		std::cout << "Your CPU doesn't support AVX - please try a non-AVX build on this machine" << std::endl;
#endif
		return false;
	}
#endif // defined (_MSC_VER) || defined (__INTEL_COMPILER)
return true; // todo: gcc detection
}

bool showBuildVersion() {
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("%s", ANDROID_STDTOC(strVersion));
#else
	std::cout << strVersion << " ";
#endif
#if defined(_M_X64) || defined(__x86_64__) || defined(__aarch64__)
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("64-bit version");
#else
	std::cout << "64-bit version";
#endif
#ifdef USE_AVX
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT(" AVX build ... ");
#else
	std::cout << " AVX build ... ";
#endif
	if (!checkAVX())
		return false;
#ifdef USE_FMA
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("using FMA (Fused Multiply-Add) instruction ... ");
#else
	std::cout << "\nusing FMA (Fused Multiply-Add) instruction ... ";
#endif
#endif
#endif // USE_AVX
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("");
#else
	std::cout << std::endl;
#endif
#else
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("32-bit version");
#else
	std::cout << "32-bit version";
#endif
#if defined(USE_SSE2)
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT(", SSE2 build ... ");
#else
	std::cout << ", SSE2 build ... ";
#endif
	// Verify processor capabilities:
	if (!checkSSE2())
		return false;
#endif // defined(USE_SSE2)
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("");
#else
	std::cout << "\n" << std::endl;
#endif
#endif
	return true;
}

void showCompiler() {
	// https://sourceforge.net/p/predef/wiki/Compilers/
#if defined (__clang__)
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("Clang %d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
#else
	std::cout << "Clang " << __clang_major__ << "."
	<< __clang_minor__ << "."
	<< __clang_patchlevel__ << std::endl;
#endif
#elif defined (__MINGW64__)
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("minGW-w64");
#else
	std::cout << "minGW-w64" << std::endl;
#endif
#elif defined (__MINGW32__)
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("minGW");
#else
	std::cout << "minGW" << std::endl;
#endif
#elif defined (__GNUC__)
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("gcc %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
	std::cout << "gcc " << __GNUC__ << "."
	<< __GNUC_MINOR__ << "."
	<< __GNUC_PATCHLEVEL__ << std::endl;
#endif
#elif defined (_MSC_VER)
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("Visual C++ %d", _MSC_FULL_VER); // assume int
#else
	std::cout << "Visual C++ " << _MSC_FULL_VER << std::endl;
#endif
#elif defined (__INTEL_COMPILER)
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("Intel Compiler %d", __INTEL_COMPILER); // assume int
#else
	std::cout << "Intel Compiler " << __INTEL_COMPILER << std::endl;
#endif
#else
#ifdef COMPILING_ON_ANDROID
    ANDROID_OUT("unknown");
#else
	std::cout << "unknown" << std::endl;
#endif
#endif

}

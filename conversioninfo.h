/*
* Copyright (C) 2016 - 2019 Judd Niemann - All Rights Reserved.
* You may use, distribute and modify this code under the
* terms of the GNU Lesser General Public License, version 2.1
*
* You should have received a copy of GNU Lesser General Public License v2.1
* with this file. If not, please refer to: https://github.com/jniemann66/ReSampler
*/

#ifndef CONVERSIONINFO_H
#define CONVERSIONINFO_H

// conversioninfo.h:
// defines the ConversionInfo struct,
// for holding conversion parameters.

#include "ditherer.h"
#include "csv.h"

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>

typedef enum {
	relaxed,
	normal,
	steep,
	custom
} LPFMode;

// The following functions are used for fetching commandline parameters:

// sanitize() : function to allow more permissive parsing.
// Removes hyphens after first non-hyphen character and converts to lowercase.
// examples:
//  --flatTPDF  => --flattpdf
//  --flat-tpdf => --flattpdf

std::string sanitize(const std::string& str) {
	std::string r(str);
	auto s = r.find_first_not_of('-'); // get position of first non-hyphen
	r.erase(std::remove(r.begin() + s, r.end(), '-'), r.end()); // remove all hyphens after the first non-hyphen
	std::transform(r.begin(), r.end(), r.begin(), ::tolower); // change to lower-case
	return r;
}

// for numeric parameter values:
template<typename T>
bool getCmdlineParam(char** begin, char** end, const std::string& option, T& parameter) {
	std::vector<std::string> args(begin, end);
	bool found = false;
	for (auto it = args.begin(); it != args.end(); it++) {
		if (sanitize(*it) == sanitize(option)) {
			found = true;
			auto next = std::next(it);
			if (next != args.end()) {
				try {
                    parameter = static_cast<T>(std::stof(*next));
				}
				catch (std::invalid_argument& e) {
					// leave parameter unchanged
				}
			}
			break;
		}
	}
	return found;
}

// for string parameter values:
bool getCmdlineParam(char** begin, char** end, const std::string& option, std::string& parameter)
{
	std::vector<std::string> args(begin, end);
	bool found = false;
	for (auto it = args.begin(); it != args.end(); it++) {
		if (sanitize(*it) == sanitize(option)) {
			found = true;
			auto next = std::next(it);
			if (next != args.end())
				parameter = *next;
			break;
		}
	}
	return found;
}

// switch only (no parameter value)
bool getCmdlineParam(char** begin, char** end, const std::string& option)
{
	bool found = false;
	std::vector<std::string> args(begin, end);
	for (auto &arg : args) {
		if (sanitize(arg) == sanitize(option)) {
			found = true;
			break;
		}
	}
	return found;
}

// struct ConversionInfo : structure for holding all the parameters required for a conversion job

struct ConversionInfo
{
	std::string inputFilename;
	std::string outputFilename;
    int inputSampleRate;
    int outputSampleRate;
	double gain;
	double limit;
	bool bUseDoublePrecision;
	bool bNormalize;
	double normalizeAmount;
	int outputFormat;
	std::string outBitFormat;
	bool bDither;
	double ditherAmount;
	int ditherProfileID;
	bool bAutoBlankingEnabled;
	bool bDelayTrim;
	bool bMinPhase;
	bool bSetFlacCompression;
	int flacCompressionLevel;
	bool bSetVorbisQuality;
	double vorbisQuality;
	bool disableClippingProtection;
	LPFMode lpfMode;
	double lpfCutoff;
	double lpfTransitionWidth;
	bool bUseSeed;
	int seed;
	bool dsfInput;
	bool dffInput;
	bool csvOutput;
	bool bEnablePeakDetection;
	bool bMultiThreaded;
	bool bRf64;
	bool bNoPeakChunk;
	bool bWriteMetaData;
	int maxStages;
	bool bSingleStage;
	bool bMultiStage;
	bool bShowStages;
	int overSamplingFactor;
	bool bBadParams;
	std::string appName;

#if defined (_WIN32) || defined (_WIN64)
	std::string tmpDir;
#endif
	
	bool bTmpFile;
	bool bShowTempFile;
	bool quantize;
	int quantizeBits;
	IntegerWriteScalingStyle integerWriteScalingStyle;

	bool fromCmdLineArgs(int argc, char* argv[]);
	std::string toCmdLineArgs();
};


inline std::string ConversionInfo::toCmdLineArgs() {
	std::vector<std::string> args;
	std::string result;

	args.emplace_back("-i");
	args.push_back(inputFilename);
	args.emplace_back("-o");
	args.push_back(outputFilename);
	args.emplace_back("-r");
	args.push_back(std::to_string(outputSampleRate));

	if(bUseDoublePrecision)
		args.emplace_back("--doubleprecision");

	if(bNormalize) {
		args.emplace_back("-n");
		args.push_back(std::to_string(normalizeAmount));
	}

	if(bMinPhase)
		args.emplace_back("--minphase");

	if (lpfMode == custom) {
		args.emplace_back("--lpf-cutoff");
		args.push_back(std::to_string(lpfCutoff));
		args.emplace_back("--lpf-transition");
		args.push_back(std::to_string(lpfTransitionWidth));
	}

	if (maxStages == 1) {
		args.emplace_back("--maxStages");
		args.push_back(std::to_string(maxStages));
	}

	for(auto it = args.begin(); it != args.end(); it++) {
		result.append(*it);
		if(it != std::prev(args.end()))
			result.append(" ");
	}

	return result;
}

// fromCmdLineArgs()
// Return value indicates whether caller should continue execution (ie true: continue, false: terminate)
// Some commandline options (eg --version) should result in termination, but not error.
// unacceptable parameters are indicated by setting bBadParams to true

inline bool ConversionInfo::fromCmdLineArgs(int argc, char* argv[]) {

	// set defaults for EVERYTHING: 
	inputFilename.clear();
	outputFilename.clear();
	inputSampleRate = 0;
	outputSampleRate = 0;
	gain = 1.0;
	limit = 1.0;
	bUseDoublePrecision = false;
	bNormalize = false;
	normalizeAmount = 1.0;
	outputFormat = 0;
	outBitFormat.clear();
	bDither = false;
	ditherAmount = 1.0;
	ditherProfileID = DitherProfileID::standard;
	bAutoBlankingEnabled = false;
	bDelayTrim = true;
	bMinPhase = false;
	bSetFlacCompression = false;
	flacCompressionLevel = 5;
	bSetVorbisQuality = true;
	vorbisQuality = 3;
	disableClippingProtection = false;
	lpfMode = normal;
	lpfCutoff = 100.0 * (10.0 / 11.0);
	lpfTransitionWidth = 100.0 - lpfCutoff;
	bUseSeed = false;
	seed = 0;
	dsfInput = false;
	dffInput = false;
	bEnablePeakDetection = true;
	bMultiThreaded = false;
	bRf64 = false;
	bNoPeakChunk = false;
	bWriteMetaData = true;
	maxStages = 3;
	bSingleStage = false;
	bMultiStage = true;
	bShowStages = false;
	bTmpFile = true;
	bShowTempFile = false;
	overSamplingFactor = 1;
	bBadParams = false;
	appName.clear();

	// get core parameters:
	getCmdlineParam(argv, argv + argc, "-i", inputFilename);
	getCmdlineParam(argv, argv + argc, "-o", outputFilename);
	getCmdlineParam(argv, argv + argc, "-r", outputSampleRate);
	getCmdlineParam(argv, argv + argc, "-b", outBitFormat);

	// get extended parameters
	getCmdlineParam(argv, argv + argc, "--gain", gain);
	bUseDoublePrecision = getCmdlineParam(argv, argv + argc, "--doubleprecision");
	disableClippingProtection = getCmdlineParam(argv, argv + argc, "--noClippingProtection");
	bNormalize = getCmdlineParam(argv, argv + argc, "-n", normalizeAmount);
	bDither = getCmdlineParam(argv, argv + argc, "--dither", ditherAmount);
	ditherProfileID = getDefaultNoiseShape(outputSampleRate);
	getCmdlineParam(argv, argv + argc, "--ns", ditherProfileID);
	ditherProfileID = getCmdlineParam(argv, argv + argc, "--flat-tpdf") ? DitherProfileID::flat : ditherProfileID;
	bAutoBlankingEnabled = getCmdlineParam(argv, argv + argc, "--autoblank");
	bUseSeed = getCmdlineParam(argv, argv + argc, "--seed", seed);
	bDelayTrim = !getCmdlineParam(argv, argv + argc, "--noDelayTrim");
	bMinPhase = getCmdlineParam(argv, argv + argc, "--minphase");
	bSetFlacCompression = getCmdlineParam(argv, argv + argc, "--flacCompression", flacCompressionLevel);
	bSetVorbisQuality = getCmdlineParam(argv, argv + argc, "--vorbisQuality", vorbisQuality);
	bMultiThreaded = getCmdlineParam(argv, argv + argc, "--mt");
	bRf64 = getCmdlineParam(argv, argv + argc, "--rf64");
	bNoPeakChunk = getCmdlineParam(argv, argv + argc, "--noPeakChunk");
	bWriteMetaData = !getCmdlineParam(argv, argv + argc, "--noMetadata");
	getCmdlineParam(argv, argv + argc, "--maxStages", maxStages);
	bSingleStage = getCmdlineParam(argv, argv + argc, "--singleStage");
	bMultiStage = getCmdlineParam(argv, argv + argc, "--multiStage");
	integerWriteScalingStyle = getCmdlineParam(argv, argv + argc, "--pow2clip") ? IntegerWriteScalingStyle::Pow2Clip : IntegerWriteScalingStyle::Pow2Minus1;

#if defined (_WIN32) || defined (_WIN64)
	getCmdlineParam(argv, argv + argc, "--tempDir", tmpDir);
#endif

	bTmpFile = !getCmdlineParam(argv, argv + argc, "--noTempFile");
	bShowTempFile = getCmdlineParam(argv, argv + argc, "--showTempFile");

	/* resolve conflicts between singleStage and multiStage, according to this table:
	IN   OUT
	s m  S M
	==== ===
	F F  F T
	F T  F T (no change)
	T F  T F (no change)
	T T  F T 
	*/

	if (!bMultiStage && !bSingleStage)
		bMultiStage = true;
	else if (bMultiStage && bSingleStage)
		bSingleStage = false;

	bShowStages = getCmdlineParam(argv, argv + argc, "--showStages");

	// LPFilter settings:
	if (getCmdlineParam(argv, argv + argc, "--relaxedLPF")) {
		lpfMode = relaxed;
		lpfCutoff = 100.0 * (21.0 / 22.0);				// late cutoff
		lpfTransitionWidth = 2 * (100.0 - lpfCutoff); // wide transition (double-width)  
	}

	if (getCmdlineParam(argv, argv + argc, "--steepLPF")) {
		lpfMode = steep;
		lpfCutoff = 100.0 * (21.0 / 22.0);				// late cutoff
		lpfTransitionWidth = 100.0 - lpfCutoff;       // steep transition  
	}

	if (getCmdlineParam(argv, argv + argc, "--lpf-cutoff", lpfCutoff)) { // custom LPF cutoff frequency
		lpfMode = custom;
		if (!getCmdlineParam(argv, argv + argc, "--lpf-transition", lpfTransitionWidth)) {
			lpfTransitionWidth = 100 - lpfCutoff; // auto mode
		}
	}

	double qb = 0.0;
	quantize = getCmdlineParam(argv, argv + argc, "--quantize-bits", qb);
	quantizeBits = static_cast<int>(std::floor(qb));

	// constraining functions:
	auto constrainDouble = [](double& val, double minVal, double maxVal) {
		val = std::max(minVal, std::min(val, maxVal));
	};

	auto constrainInt = [](int& val, int minVal, int maxVal) {
		val = std::max(minVal, std::min(val, maxVal));
	};

	// set constraints:
	constrainInt(flacCompressionLevel, 0, 8);
	constrainDouble(vorbisQuality, -1, 10);
	constrainInt(maxStages, 1, 10);
	constrainDouble(lpfCutoff, 1.0, 99.9);
	constrainDouble(lpfTransitionWidth, 0.1, 400.0);

	if (bNormalize) {
		if (normalizeAmount <= 0.0)
			normalizeAmount = 1.0;
		if (normalizeAmount > 1.0)
			std::cout << "\nWarning: Normalization factor greater than 1.0 - THIS WILL CAUSE CLIPPING !!\n" << std::endl;
		limit = normalizeAmount;
	}
	
	if (bDither) {
		if (ditherAmount <= 0.0)
			ditherAmount = 1.0;
	}
	
	if (ditherProfileID < 0)
		ditherProfileID = 0;
	if (ditherProfileID >= DitherProfileID::end)
		ditherProfileID = getDefaultNoiseShape(outputSampleRate);

	// test for bad parameters:
	bBadParams = false;
	if (outputFilename.empty()) {
		if (inputFilename.empty()) {
			std::cout << "Error: Input filename not specified" << std::endl;
			bBadParams = true;
		}
		else {
			std::cout << "Output filename not specified" << std::endl;
			outputFilename = inputFilename;
			if (outputFilename.find('.') != std::string::npos) {
				auto dot = outputFilename.find_last_of('.');
				outputFilename.insert(dot, "(converted)");
			}
			else {
				outputFilename.append("(converted)");
			}
			std::cout << "defaulting to: " << outputFilename << "\n" << std::endl;
		}
	}

	else if (outputFilename == inputFilename) {
		std::cout << "\nError: Input and Output filenames cannot be the same" << std::endl;
		bBadParams = true;
	}

	if (outputSampleRate == 0) {
		std::cout << "Error: Target sample rate not specified" << std::endl;
		bBadParams = true;
	}

	if (bBadParams) {
		std::cout << strUsage << std::endl;
		return false;
	}
	return true;
}

static_assert(std::is_copy_constructible<ConversionInfo>::value,
	"ConversionInfo must be copy constructible");

static_assert(std::is_copy_assignable<ConversionInfo>::value,
	"ConversionInfo must be copy assignable");

#endif // CONVERSIONINFO_H


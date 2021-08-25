/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"

//==============================================================================
MIDIClipVariationsAudioProcessor::MIDIClipVariationsAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    tempoBpm = 120.0;
    lastBufferTimestamp = 0;
    currentAllowedChannel = 1;
    addParameter (selectedChannel = new juce::AudioParameterInt (
        "channel", // parameterID
         "Channel", // parameter name
         1,   // minimum value
         16,   // maximum value
         1)
    );
    addParameter (phraseBeats = new juce::AudioParameterInt (
         "phraseBeats", // parameterID
         "Beats per phrase", // parameter name
         4,   // minimum value
         16,   // maximum value
         8)
    );
}

MIDIClipVariationsAudioProcessor::~MIDIClipVariationsAudioProcessor()
{
}

//==============================================================================
const juce::String MIDIClipVariationsAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MIDIClipVariationsAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool MIDIClipVariationsAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool MIDIClipVariationsAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double MIDIClipVariationsAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MIDIClipVariationsAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int MIDIClipVariationsAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MIDIClipVariationsAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String MIDIClipVariationsAudioProcessor::getProgramName (int index)
{
    return {};
}

void MIDIClipVariationsAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void MIDIClipVariationsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
}

void MIDIClipVariationsAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool MIDIClipVariationsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

double MIDIClipVariationsAudioProcessor::samplesToBeats(juce::int64 timestamp) {
    double secondsPerBeat = 60.0 / tempoBpm;
    double samplesPerBeat = secondsPerBeat * getSampleRate();
    
    return (timestamp / samplesPerBeat);
}

double MIDIClipVariationsAudioProcessor::beatsToPhrase(double beats) {    
    return (beats / *phraseBeats);
}

bool MIDIClipVariationsAudioProcessor::shouldPlayMidiMessage (juce::MidiMessage message, juce::int64 blockTime, juce::int64 eventTime)
{
    if (! message.isNoteOn()) {
        // pass anything except note-ons
        return true;
    }
    
    int channel = currentAllowedChannel;
    
    double blockStartBeats = samplesToBeats(blockTime);
    double eventTimeBeats = samplesToBeats(eventTime);

    double blockStartPhrase = beatsToPhrase(blockStartBeats) + 0.001;
    double eventTimePhrase = beatsToPhrase(eventTimeBeats) + 0.001;

    // Determine whether to use current channel or param channel for this event.
    // If phrase boundary has occurred since start of block, use param.
    if ( std::floor(blockStartPhrase) < std::floor(eventTimePhrase) ) {
        channel = *selectedChannel;
    }

    return (message.getChannel() == channel);
}


void MIDIClipVariationsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    outputMidiBuffer.clear();
  
    const int allowChannel = *selectedChannel;

    juce::int64 playheadTimeSamples = 0;

    juce::AudioPlayHead::CurrentPositionInfo playheadPosition;
    juce::AudioPlayHead* playhead = AudioProcessor::getPlayHead();
    if (playhead) {
        playhead->getCurrentPosition(playheadPosition);
        playheadTimeSamples = playheadPosition.timeInSamples;
        tempoBpm = playheadPosition.bpm;
    
        if (! playheadPosition.isPlaying) {
            currentAllowedChannel = allowChannel;
        }
        else {
            // Determine if the last block straddled a phrase boundary.
            // If so, apply the channel param.
            double blockStartBeats = samplesToBeats(playheadTimeSamples);
            double previousBeats = samplesToBeats(lastBufferTimestamp);

            double blockStartPhrase = beatsToPhrase(blockStartBeats) + 0.001;
            double prevPhrase = beatsToPhrase(previousBeats) + 0.001;

            if ( std::floor(prevPhrase) < std::floor(blockStartPhrase) ) {
                currentAllowedChannel = allowChannel;
            }
        }
    }
    else {
        currentAllowedChannel = allowChannel;
    }

    for (auto m: midiMessages)
    {
        auto message = m.getMessage();
        auto timestamp = message.getTimeStamp();
        
        if (this->shouldPlayMidiMessage(message, playheadTimeSamples, playheadTimeSamples + timestamp)) {
            outputMidiBuffer.addEvent(message, timestamp);
        }

    }

    midiMessages.swapWith(outputMidiBuffer);
    
    lastBufferTimestamp = playheadTimeSamples;
}

//==============================================================================
bool MIDIClipVariationsAudioProcessor::hasEditor() const
{
    return false;
}

juce::AudioProcessorEditor* MIDIClipVariationsAudioProcessor::createEditor()
{
    return 0;
}

//==============================================================================
void MIDIClipVariationsAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void MIDIClipVariationsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MIDIClipVariationsAudioProcessor();
}

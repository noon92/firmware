#pragma once

#include "configuration.h"
#include "main.h"
#include "SinglePortModule.h"
#include "concurrency/Periodic.h"
#define REMOTE_RANGETEST_DURATION 90 //length of range test in minutes
#define REMOTE_RANGETEST_COVERAGE_DURATION 60 //length of coverage test in minutes
#define REMOTE_RANGETEST_INTERVAL 0 //time before new range test can be called after previous one ends
#define REMOTE_RANGETEST_TRIGGER "Range test"
#define REMOTE_RANGETEST_LISTEN_CHANINDEX 0 //channel on which response is given and encryption key on which command is listened for
class RemoteRangetestModule : public SinglePortModule, public concurrency::OSThread
{
  public:
    RemoteRangetestModule();

  protected:
    // New text message from the mesh
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    // Send a message out over the mesh
    void sendText(const char *message, int channelIndex, uint32_t dest);
    void sendText(const char *message, int channelIndex) { sendText(message, channelIndex, NODENUM_BROADCAST); }
    void sendText(const char *message) { sendText(message, 0, NODENUM_BROADCAST); }
    // Overloaded methods instead of default args, otherwise compiler complains about passing string literals

    // Timer, to run code later
    virtual int32_t runOnce() override;

    // Compare two strings, case insensitive
    static bool stringsMatch(const char *s1, const char *s2, bool caseSensitive = false);

    // Code for remotely starting range test
    void beginRangeTest(uint32_t replyTo, ChannelIndex replyVia);
    bool ranRangeTest = false;
    bool waitingToReboot = false;

    // Coverage Test (DMs)
    void startCoverageTest(uint32_t recipient, uint8_t channelIndex);
    void sendCoverageTestDM();
    bool coverageTestRunning = false;
    uint32_t coverageTestRunningSinceMs;
    uint32_t coverageTestRecipient;
    uint32_t coverageTestChannelIndex;
    uint32_t coverageTestMessageCount;

};

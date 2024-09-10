#include "RemoteRangeTestModule.h"
#include "MeshService.h"
#include "Router.h"

static const int durationMinutes = REMOTE_RANGETEST_DURATION; // How long to run the range test
static const int intervalMinutes = REMOTE_RANGETEST_INTERVAL; // How often can range tests run
static const int coverDurationMinutes = REMOTE_RANGETEST_COVERAGE_DURATION;
static char remoteRangeTestTriggerWord[] = REMOTE_RANGETEST_TRIGGER;
uint8_t channelIndex = REMOTE_RANGETEST_LISTEN_CHANINDEX;
uint32_t destinationNode;

// Do at startup
RemoteRangetestModule::RemoteRangetestModule()
    : SinglePortModule("RemoteRangeTest", meshtastic_PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("RemoteRangeTest")
{
    // If range test enabled
    if (moduleConfig.range_test.enabled)
    {
        ranRangeTest = true;                         // Impose the rate-limiting interval
        setInterval(durationMinutes * MS_IN_MINUTE); // Set     timer to disable range test
        LOG_INFO("Will disable range test in %i minutes\n", durationMinutes);
    }
    else
        disable(); // No timer
}

// Do something when text is received
ProcessMessage RemoteRangetestModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Grab info from the received protobuf
    char *text = (char *)mp.decoded.payload.bytes;

    uint8_t hopsAway = mp.hop_start - mp.hop_limit;
    destinationNode = mp.to;

    if (stringsMatch(text, "Cover test") && (mp.to == myNodeInfo.my_node_num))
    {
        LOG_INFO("Coverage test requested by %u via DM.\n", mp.from);
        String reply = "Starting ";
        reply.concat(coverDurationMinutes);
        reply.concat("min coverage test");
        sendText(reply.c_str(), mp.channel, mp.from);
        startCoverageTest(mp.from, mp.channel);
    }

    if (stringsMatch(text, "/help"))
    {
        LOG_INFO("Help requested by %u\n", mp.from);
        sendText("1/4\n'SNR': In chan responds to neighbors. In DM to all. Format: SNR/RSSI/hopsAway", mp.channel, mp.from);
        String reply2 = "2/4\n'Range test': DM only. Sends RT for ";
        reply2.concat(durationMinutes);
        reply2.concat("mins");
        sendText(reply2.c_str(), mp.channel, mp.from);
        String reply3 = "3/4\n'Cover test': DM only. Sends CT every 1min for ";
        reply3.concat(coverDurationMinutes);
        reply3.concat("min in DM w/7hops");
        sendText(reply3.c_str(), mp.channel, mp.from);
        sendText("4/4\n'Stop': DM only. Stops CT & RT", mp.channel, mp.from);
        nodeDB->saveToDisk(SEGMENT_MODULECONFIG); // Save this changed config to disk
    }

    if ((stringsMatch(text, "stop")) && (mp.to == myNodeInfo.my_node_num))
    {
        LOG_INFO("Stop range/coverage test requested by %u\n", mp.from);
        sendText("Stopping", mp.channel, mp.from);
        coverageTestRunning = false;
        moduleConfig.range_test.enabled = false;
        nodeDB->saveToDisk(SEGMENT_MODULECONFIG); // Save this changed config to disk
    }

    if (stringsMatch(text, "SNR"))
    {
        if ((mp.to != myNodeInfo.my_node_num) & (hopsAway != 0)){ //was this a channel message and sent from a node that is not a neighbor?
            LOG_INFO("SNR requested by %u via channel but was %i hops (%i - %i) away.\n", mp.from, hopsAway, mp.hop_start, mp.hop_limit);
        } else {
            LOG_INFO("SNR requested by %u: %.1f SNR, %i hops (%i - %i)\n", mp.from, mp.rx_snr, hopsAway, mp.hop_start, mp.hop_limit);
            char message[20];
            snprintf(message, sizeof(message), "%.1f/%i/%i", mp.rx_snr, mp.rx_rssi, hopsAway);
            sendText(message, mp.channel, mp.from);
        }
    }

    if (stringsMatch(text, remoteRangeTestTriggerWord) && mp.to == myNodeInfo.my_node_num)
    {
        LOG_INFO("User asked for a rangetest\n");
        beginRangeTest(mp.from, channelIndex);
        return ProcessMessage::CONTINUE;
    }

    return ProcessMessage::CONTINUE; // We weren't interested in this message, treat it as normal
}

// Check if it's appropriate, then run range test
void RemoteRangetestModule::beginRangeTest(uint32_t informNode, ChannelIndex informViaChannel)
{
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(informNode); // Save typing

    String nodeName; // get short nodename or if not present in nodedb, just output the nodenum
    if (node && node->has_user && strlen(node->user.short_name) > 0)
        nodeName.concat(node->user.short_name);
    else
        nodeName.concat(informNode);

    // Abort: if already running
    if (moduleConfig.range_test.enabled)
    {
        LOG_INFO("Remote range test already running\n");

        String reply = nodeName;
        reply.concat(": Range test running. Turn on in settings, with 0secs or off as interval");
        sendText(reply.c_str(), informViaChannel);
        return;
    }

    // Abort: if there was a previous test recently
    if (ranRangeTest && (millis() < intervalMinutes * MS_IN_MINUTE))
    {
        LOG_INFO("Too soon for another remote range test\n");

        String reply = nodeName;
        reply.concat(": Too soon for a new range test. Try again in ");
        reply.concat(intervalMinutes - (millis() / MS_IN_MINUTE));
        reply.concat("mins");
        sendText(reply.c_str(), informViaChannel, NODENUM_BROADCAST);
        return;
    }

    // Looks okay to start range test
    // Set the module config, then reboot

    LOG_INFO("Looks okay: enabling remote range test\n");
    LOG_INFO("INFORMNODE: %i\n", informNode);

    String reply = nodeName;
    reply.concat(": Starting ");
    reply.concat(coverDurationMinutes);
    reply.concat("min range test");
    sendText(reply.c_str(), informViaChannel);
    moduleConfig.range_test.enabled = true;   // Enable the range test module
    nodeDB->saveToDisk(SEGMENT_MODULECONFIG); // Save this changed config to disk

    // Reboot in 15 seconds
    rebootAtMsec = millis() + (15 * 1000UL); 
}

// Timer: when we need to wait in the background
int32_t RemoteRangetestModule::runOnce()
{
    // If the range test module is running, and is due to be disabled
    if (moduleConfig.range_test.enabled && millis() > durationMinutes * 60 * 1000UL)
    {
        LOG_INFO("Time's up! Disabling remote range test\n");
        sendText("Range test complete", channelIndex);
        moduleConfig.range_test.enabled = false;
        nodeDB->saveToDisk(SEGMENT_MODULECONFIG); // Save this changed config to disk
    }

    // If the coverage test is running, and due to fire a message (every minute)
    if (coverageTestRunning) {
        // Send the message
        LOG_DEBUG("Coverage test: sending DM to %u on channel %u\n", coverageTestRecipient, (uint32_t)channelIndex);
        sendCoverageTestDM();

        // If running for longer than durationMinutes: mark as finished
        if (millis() > coverageTestRunningSinceMs + (coverDurationMinutes * 60 * 1000UL)) {
            LOG_INFO("Coverage test complete\n");
            coverageTestRunning = false;
        }
    }

    // Decide if we still need to keep our timer running, for either remote rangetest or coverage test
    if (coverageTestRunning || moduleConfig.range_test.enabled) {
        // Run again in a minute
        // For coverage test: this is how often we send out DMs
        // For range test: probably only reach this code if we are *also* running a coverage test
        return 60 * 1000UL;
    }
    else
        return OSThread::disable(); // Nobody using the timer
}

// Send a text message over the mesh. "Borrowed" from canned message module
void RemoteRangetestModule::sendText(const char *message, int channelIndex, uint32_t dest)
{
    meshtastic_MeshPacket *p = router->allocForSending();
    p->to = dest;
    p->channel = channelIndex;
    p->want_ack = false;
    if ((coverageTestRunning) || ((destinationNode == myNodeInfo.my_node_num) & (!coverageTestRunning) & (!moduleConfig.range_test.enabled))) { //if coverage test OR snr via DM, respond with 7 hops.
        p->hop_limit = 7;
        LOG_INFO("Sending response with 7 hop limit.\n");
    }
    p->decoded.payload.size = strlen(message);
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);

    LOG_DEBUG("Sending message id=%d, dest=%x, msg=%.*s\n", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);

    service -> sendToMesh(p, RX_SRC_LOCAL, true);
}

// Check if two strings match. Option to ignore the case when comparing
bool RemoteRangetestModule::stringsMatch(const char *s1, const char *s2, bool caseSensitive)
{
    // If different length, no match
    if (strlen(s1) != strlen(s2))
        return false;

    // Compare character by character (possible case-insensitive)
    for (uint16_t i = 0; i <= strlen(s1); i++)
    {
        if (caseSensitive && s1[i] != s2[i])
            return false;
        if (!caseSensitive && tolower(s1[i]) != tolower(s2[i]))
            return false;
    }

    return true;
}

void RemoteRangetestModule::startCoverageTest(uint32_t recipient, uint8_t channelIndex) {
    // Store details of where we're sending the DMs
    coverageTestRecipient = recipient;
    coverageTestChannelIndex = channelIndex;

    // Reset the number send with the messages
    coverageTestMessageCount = 1;

    // Start the thread running (runOnce timet)
    coverageTestRunning = true;
    OSThread::setInterval(0);
    OSThread::enabled = true;

    LOG_INFO("Coverage test started\n");
}

void RemoteRangetestModule::sendCoverageTestDM() {
    char message[25];
    snprintf(message, sizeof(message), "cov%u", coverageTestMessageCount);
    sendText(message, coverageTestChannelIndex, coverageTestRecipient);
    coverageTestMessageCount++;
}
#ifndef __RTMP_PROTOCOL_H__
#define __RTMP_PROTOCOL_H__

#include <stdint.h>
#include <stddef.h>

#include <map>
#include <sstream>
#include <set>

#include "crc32.h"
#include "media_publisher.h"
#include "media_subscriber.h"
#include "ref_ptr.h"
#include "server_protocol.h"
#include "socket_util.h"
#include "trace_tool.h"

using std::map;
using std::string;
using std::ostringstream;
using std::set;

class AmfCommand;
class Epoller;
class Fd;
class HttpFlvProtocol;
class IoBuffer;
class RtmpMgr;
class TcpSocket;

enum HandShakeStatus
{
    kStatus_0 = 0,
    kStatus_1,
    kStatus_2,
    kStatus_Done,
};

enum RtmpMessageType
{
    kSetChunkSize = 1,
    kAcknowledgement = 3,
    kUserControlMessage = 4,
    kWindowAcknowledgementSize = 5,
    kSetPeerBandwidth = 6,

    kAudio        = 8,
    kVideo        = 9,

    kAmf3Command = 17,
    kMetaData    = 18,
    kAmf0Command = 20,
};

enum RtmpRole
{
    // other_server --> me --> client

    kUnknownRtmpRole = -1,
    kClientPush  = 0,
    kPushServer  = 1,
    kPullServer  = 2,
    kClientPull  = 3,
};

struct RtmpUrl
{
    std::string ip;
    uint16_t port;
    std::string app;
    std::string stream_name;
};

struct RtmpMessage
{
    RtmpMessage()
        :
        cs_id(0),
        timestamp(0),
        timestamp_delta(0),
        timestamp_calc(0),
        message_length(0),
        message_type_id(0),
        message_stream_id(0)
    {
    }

    string ToString() const
    {
        ostringstream os;

        os << "cs_id:" << cs_id
           << ",timestamp:" << timestamp
           << ",timestamp_delta:" << timestamp_delta
           << ",timestamp_calc:" << timestamp_calc
           << ",message_length:" << message_length
           << ",message_type_id:" << (uint16_t)message_type_id
           << ",message_stream_id:" << message_stream_id
           << ",msg:" << (uint64_t)msg
           << ",len:" << len;

        return os.str();
    }

    uint32_t cs_id;
    uint32_t timestamp;
    uint32_t timestamp_delta;
    uint32_t timestamp_calc;
    uint32_t message_length;
    uint8_t  message_type_id;
    uint32_t message_stream_id;

    uint8_t* msg;
    uint32_t len;
};

class RtmpProtocol : public MediaPublisher, public MediaSubscriber
{
public:
    RtmpProtocol(Epoller* epoller, Fd* socket);
    ~RtmpProtocol();

    bool IsServerRole()
    {
        return role_ == kClientPull || role_ == kClientPush;
    }

    bool IsClientRole()
    {
        return role_ == kPushServer || role_ == kPullServer;
    }

    void SetClientPush()
    {
        role_ = kClientPush;
    }

    void SetPushServer()
    {
        role_ = kPushServer;
    }

    void SetPullServer()
    {
        role_ = kPullServer;
    }

    void SetClientPull()
    {
        role_ = kClientPull;
    }

    void SetRole(const RtmpRole& role)
    {
        role_ = role;
    }

    void SetMediaPublisher(MediaPublisher* media_publisher)
    {
        media_publisher_ = media_publisher;
    }

    int Parse(IoBuffer& io_buffer);
    int OnStop();
    int OnConnected();

    int EveryNSecond(const uint64_t& now_in_ms, const uint32_t& interval, const uint64_t& count);

    int HandShakeStatus0();
    int HandShakeStatus1();
    int SetOutChunkSize(const uint32_t& chunk_size);
    int SetWindowAcknowledgementSize(const uint32_t& ack_window_size);
    int SetPeerBandwidth(const uint32_t& ack_window_size, const uint8_t& limit_type);
    int SendUserControlMessage(const uint16_t& event, const uint32_t& data);
    int SendConnect(const string& url);
    int SendCreateStream();
    int SendReleaseStream();
    int SendFCPublish();
    int SendCheckBw();
    int SendPublish(const double& stream_id);
    int SendAudio(const RtmpMessage& audio);
    int SendVideo(const RtmpMessage& video);

    void SetApp(const string& app)
    {
        app_ = app;
        media_muxer_.SetApp(app_);
    }

    void SetStreamName(const string& stream_name)
    {
        stream_name_ = stream_name;
        media_muxer_.SetStreamName(stream_name_);
    }

    int ConnectForwardRtmpServer(const string& ip, const uint16_t& port);
    int ConnectFollowServer(const string& ip, const uint16_t& port);

    static int ParseRtmpUrl(const string& url, RtmpUrl& rtmp_url);

    TcpSocket* GetTcpSocket()
    {
        return (TcpSocket*)socket_;
    }

    bool CanPublish()
    {
        return can_publish_;
    }

    int SendRtmpMessage(const uint32_t cs_id, const uint32_t& message_stream_id, const uint8_t& message_type_id, const uint8_t* data, const size_t& len);
    int SendMediaData(const Payload& media);

	virtual int SendVideoHeader(const string& header);
    virtual int SendAudioHeader(const string& header);
    virtual int SendMetaData(const string& metadata);

private:
    double GetTransactionId()
    {
        transaction_id_ += 1.0;

        return transaction_id_;
    }

    string DumpIdCommand()
    {
        ostringstream os;
        for (const auto& kv : id_command_)
        {
            os << kv.first << "=>" << kv.second << ",";
        }

        return os.str();
    }

    int OnConnectCommand(AmfCommand& amf_command);
    int OnCreateStreamCommand(RtmpMessage& rtmp_msg, AmfCommand& amf_command);
    int OnPlayCommand(RtmpMessage& rtmp_msg, AmfCommand& amf_command);
    int OnPublishCommand(RtmpMessage& rtmp_msg, AmfCommand& amf_command);
    int OnResultCommand(AmfCommand& amf_command);
    int OnStatusCommand(AmfCommand& amf_command);

    int OnAmf0Message(RtmpMessage& rtmp_msg);
    int OnAudio(RtmpMessage& rtmp_msg);
    int OnSetChunkSize(RtmpMessage& rtmp_msg);
    int OnAcknowledgement(RtmpMessage& rtmp_msg);
    int OnUserControlMessage(RtmpMessage& rtmp_msg);
    int OnVideo(RtmpMessage& rtmp_msg);
    int OnWindowAcknowledgementSize(RtmpMessage& rtmp_msg);
    int OnSetPeerBandwidth(RtmpMessage& rtmp_msg);
    int OnMetaData(RtmpMessage& rtmp_msg);

    int OnVideoHeader(RtmpMessage& rtmp_msg);

    int OnRtmpMessage(RtmpMessage& rtmp_msg);
    int SendData(const RtmpMessage& cur_info, const Payload& paylod = Payload());

private:
    Epoller* epoller_;
    Fd* socket_;
    HandShakeStatus handshake_status_;

    int role_;

    uint32_t in_chunk_size_;
    uint32_t out_chunk_size_;

    map<uint32_t, RtmpMessage> csid_head_;
    map<uint32_t, RtmpMessage> csid_pre_info_;

    string app_;
    string tc_url_;
    string stream_name_;

    double transaction_id_;

    MediaPublisher* media_publisher_;

    string last_send_command_;

    map<double, string> id_command_;

    bool can_publish_;

    uint64_t video_frame_send_;
    uint64_t audio_frame_send_;

    uint32_t last_video_timestamp_;
    uint32_t last_video_timestamp_delta_;
    uint32_t last_audio_timestamp_;
    uint32_t last_audio_timestamp_delta_;
    uint32_t last_video_message_length_;
    uint32_t last_audio_message_length_;
    uint8_t last_message_type_id_;
};

#endif // __RTMP_PROTOCOL_H__
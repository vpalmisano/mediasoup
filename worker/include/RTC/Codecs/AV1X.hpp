#ifndef MS_RTC_CODECS_AV1X_HPP
#define MS_RTC_CODECS_AV1X_HPP

#include "common.hpp"
#include "RTC/Codecs/PayloadDescriptorHandler.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/SeqManager.hpp"

/*  WIP
    https://aomediacodec.github.io/av1-rtp-spec/
    https://aomediacodec.github.io/av1-spec/av1-spec.pdf
    * AV1 Payload Descriptor

0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|Z|Y|0 0|N|-|-|-|  OBU element 1 size (leb128)  |               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+               |
:                                                               :
:                      OBU element 1 data                       :
:                                                               :
|                                                               |
|                               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                               |  OBU element 2 size (leb128)  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
:                                                               :
:                       OBU element 2 data                      :
:                                                               :
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
:                                                               :
:                              ...                              :
:                                                               :
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|OBU e... N size|                                               |
+-+-+-+-+-+-+-+-+       OBU element N data      +-+-+-+-+-+-+-+-+
|                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Z: MUST be set to 1 if the first OBU element is an OBU fragment that is a continuation of an OBU fragment from the previous packet, and MUST be set to 0 otherwise.
Y: MUST be set to 1 if the last OBU element is an OBU fragment that will continue in the next packet, and MUST be set to 0 otherwise.
W: two bit field that describes the number of OBU elements in the packet. 
    This field MUST be set equal to 0 or equal to the number of OBU elements contained in the packet. If set to 0, each OBU element MUST be preceded by a length field. If not set to 0 (i.e., W = 1, 2 or 3) the last OBU element MUST NOT be preceded by a length field. Instead, the length of the last OBU element contained in the packet can be calculated as follows:
    Length of the last OBU element = 
    length of the RTP payload
    - length of aggregation header
    - length of previous OBU elements including length fields
N: MUST be set to 1 if the packet is the first packet of a coded video sequence, and MUST be set to 0 otherwise.

 */

namespace RTC
{
	namespace Codecs
	{
		class AV1X
		{
		public:
			struct PayloadDescriptor : public RTC::Codecs::PayloadDescriptor
			{
				/* Pure virtual methods inherited from RTC::Codecs::PayloadDescriptor. */
				~PayloadDescriptor() = default;

				void Dump() const override;

				// Header.
				uint8_t i : 1; // I: Picture ID (PID) present.
				uint8_t p : 1; // P: Inter-picture predicted layer frame.
				uint8_t l : 1; // L: Layer indices present.
				uint8_t f : 1; // F: Flexible mode.
				uint8_t b : 1; // B: Start of a layer frame.
				uint8_t e : 1; // E: End of a layer frame.
				uint8_t v : 1; // V: Scalability structure (SS) data present.
				// Extension fields.
				uint16_t pictureId{ 0 };
				uint8_t slIndex{ 0 };
				uint8_t tlIndex{ 0 };
				uint8_t tl0PictureIndex;
				uint8_t switchingUpPoint : 1;
				uint8_t interLayerDependency : 1;
				// Parsed values.
				bool isKeyFrame{ false };
				bool hasPictureId{ false };
				bool hasOneBytePictureId{ false };
				bool hasTwoBytesPictureId{ false };
				bool hasSlIndex{ false };
				bool hasTl0PictureIndex{ false };
				bool hasTlIndex{ false };
			};

		public:
			static AV1X::PayloadDescriptor* Parse(
			  const uint8_t* data,
			  size_t len,
			  RTC::RtpPacket::FrameMarking* frameMarking = nullptr,
			  uint8_t frameMarkingLen                    = 0);
			static void ProcessRtpPacket(RTC::RtpPacket* packet);

		public:
			class EncodingContext : public RTC::Codecs::EncodingContext
			{
			public:
				explicit EncodingContext(RTC::Codecs::EncodingContext::Params& params)
				  : RTC::Codecs::EncodingContext(params)
				{
				}
				~EncodingContext() = default;

				/* Pure virtual methods inherited from RTC::Codecs::EncodingContext. */
			public:
				void SyncRequired() override
				{
					this->syncRequired = true;
				}

			public:
				RTC::SeqManager<uint16_t> pictureIdManager;
				bool syncRequired{ false };
			};

			class PayloadDescriptorHandler : public RTC::Codecs::PayloadDescriptorHandler
			{
			public:
				explicit PayloadDescriptorHandler(PayloadDescriptor* payloadDescriptor);
				~PayloadDescriptorHandler() = default;

			public:
				void Dump() const override
				{
					this->payloadDescriptor->Dump();
				}
				bool Process(RTC::Codecs::EncodingContext* encodingContext, uint8_t* data, bool& marker) override;
				void Restore(uint8_t* data) override;
				uint8_t GetSpatialLayer() const override
				{
					return this->payloadDescriptor->hasSlIndex ? this->payloadDescriptor->slIndex : 0u;
				}
				uint8_t GetTemporalLayer() const override
				{
					return this->payloadDescriptor->hasTlIndex ? this->payloadDescriptor->tlIndex : 0u;
				}
				bool IsKeyFrame() const override
				{
					return this->payloadDescriptor->isKeyFrame;
				}

			private:
				std::unique_ptr<PayloadDescriptor> payloadDescriptor;
			};
		};
	} // namespace Codecs
} // namespace RTC

#endif
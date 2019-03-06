// WARNING: This file is machine generated by fidlgen.

#include <fidl_llcpp_controlflow.h>
#include <memory>

namespace fidl {
namespace test {
namespace llcpp {
namespace controlflow {

namespace {

[[maybe_unused]]
constexpr uint32_t kControlFlow_Shutdown_Ordinal = 2097819959u;
[[maybe_unused]]
constexpr uint32_t kControlFlow_NoReplyMustSendAccessDeniedEpitaph_Ordinal = 557953488u;
[[maybe_unused]]
constexpr uint32_t kControlFlow_MustSendAccessDeniedEpitaph_Ordinal = 1394461447u;

}  // namespace

zx_status_t ControlFlow::SyncClient::Shutdown() {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<ShutdownRequest>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _request = *reinterpret_cast<ShutdownRequest*>(_write_bytes);
  _request._hdr.ordinal = kControlFlow_Shutdown_Ordinal;
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(ShutdownRequest));
  ::fidl::DecodedMessage<ShutdownRequest> _decoded_request(std::move(_request_bytes));
  auto _encode_request_result = ::fidl::Encode(std::move(_decoded_request));
  if (_encode_request_result.status != ZX_OK) {
    return _encode_request_result.status;
  }
  return ::fidl::Write(this->channel_, std::move(_encode_request_result.message));
}


zx_status_t ControlFlow::SyncClient::NoReplyMustSendAccessDeniedEpitaph() {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<NoReplyMustSendAccessDeniedEpitaphRequest>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _request = *reinterpret_cast<NoReplyMustSendAccessDeniedEpitaphRequest*>(_write_bytes);
  _request._hdr.ordinal = kControlFlow_NoReplyMustSendAccessDeniedEpitaph_Ordinal;
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(NoReplyMustSendAccessDeniedEpitaphRequest));
  ::fidl::DecodedMessage<NoReplyMustSendAccessDeniedEpitaphRequest> _decoded_request(std::move(_request_bytes));
  auto _encode_request_result = ::fidl::Encode(std::move(_decoded_request));
  if (_encode_request_result.status != ZX_OK) {
    return _encode_request_result.status;
  }
  return ::fidl::Write(this->channel_, std::move(_encode_request_result.message));
}


zx_status_t ControlFlow::SyncClient::MustSendAccessDeniedEpitaph(int32_t* out_reply) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<MustSendAccessDeniedEpitaphRequest>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _request = *reinterpret_cast<MustSendAccessDeniedEpitaphRequest*>(_write_bytes);
  _request._hdr.ordinal = kControlFlow_MustSendAccessDeniedEpitaph_Ordinal;
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(MustSendAccessDeniedEpitaphRequest));
  ::fidl::DecodedMessage<MustSendAccessDeniedEpitaphRequest> _decoded_request(std::move(_request_bytes));
  auto _encode_request_result = ::fidl::Encode(std::move(_decoded_request));
  if (_encode_request_result.status != ZX_OK) {
    return _encode_request_result.status;
  }

  constexpr uint32_t _kReadAllocSize = ::fidl::internal::ClampedMessageSize<MustSendAccessDeniedEpitaphResponse>();
  FIDL_ALIGNDECL uint8_t _read_bytes[_kReadAllocSize];
  ::fidl::BytePart _response_bytes(_read_bytes, _kReadAllocSize);
  auto _call_result = ::fidl::Call<MustSendAccessDeniedEpitaphRequest, MustSendAccessDeniedEpitaphResponse>(
    this->channel_, std::move(_encode_request_result.message), std::move(_response_bytes));
  if (_call_result.status != ZX_OK) {
    return _call_result.status;
  }
  auto _decode_result = ::fidl::Decode(std::move(_call_result.message));
  if (_decode_result.status != ZX_OK) {
    return _decode_result.status;
  }
  auto& _response = *_decode_result.message.message();
  *out_reply = std::move(_response.reply);
  return ZX_OK;
}

zx_status_t ControlFlow::SyncClient::MustSendAccessDeniedEpitaph(::fidl::BytePart _response_buffer, int32_t* out_reply) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(MustSendAccessDeniedEpitaphRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  auto& _request = *reinterpret_cast<MustSendAccessDeniedEpitaphRequest*>(_request_buffer.data());
  _request._hdr.ordinal = kControlFlow_MustSendAccessDeniedEpitaph_Ordinal;
  _request_buffer.set_actual(sizeof(MustSendAccessDeniedEpitaphRequest));
  ::fidl::DecodedMessage<MustSendAccessDeniedEpitaphRequest> _decoded_request(std::move(_request_buffer));
  auto _encode_request_result = ::fidl::Encode(std::move(_decoded_request));
  if (_encode_request_result.status != ZX_OK) {
    return _encode_request_result.status;
  }
  auto _call_result = ::fidl::Call<MustSendAccessDeniedEpitaphRequest, MustSendAccessDeniedEpitaphResponse>(
    this->channel_, std::move(_encode_request_result.message), std::move(_response_buffer));
  if (_call_result.status != ZX_OK) {
    return _call_result.status;
  }
  auto _decode_result = ::fidl::Decode(std::move(_call_result.message));
  if (_decode_result.status != ZX_OK) {
    return _decode_result.status;
  }
  auto& _response = *_decode_result.message.message();
  *out_reply = std::move(_response.reply);
  return ZX_OK;
}

::fidl::DecodeResult<ControlFlow::MustSendAccessDeniedEpitaphResponse> ControlFlow::SyncClient::MustSendAccessDeniedEpitaph(::fidl::BytePart response_buffer) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(MustSendAccessDeniedEpitaphRequest)] = {};
  constexpr uint32_t _write_num_bytes = sizeof(MustSendAccessDeniedEpitaphRequest);
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes), _write_num_bytes);
  ::fidl::DecodedMessage<MustSendAccessDeniedEpitaphRequest> params(std::move(_request_buffer));
  params.message()->_hdr = {};
  params.message()->_hdr.ordinal = kControlFlow_MustSendAccessDeniedEpitaph_Ordinal;
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<ControlFlow::MustSendAccessDeniedEpitaphResponse>(
      _encode_request_result.status,
      _encode_request_result.error,
      ::fidl::DecodedMessage<ControlFlow::MustSendAccessDeniedEpitaphResponse>());
  }
  auto _call_result = ::fidl::Call<MustSendAccessDeniedEpitaphRequest, MustSendAccessDeniedEpitaphResponse>(
    this->channel_, std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<ControlFlow::MustSendAccessDeniedEpitaphResponse>(
      _call_result.status,
      _call_result.error,
      ::fidl::DecodedMessage<ControlFlow::MustSendAccessDeniedEpitaphResponse>());
  }
  return ::fidl::Decode(std::move(_call_result.message));
}


bool ControlFlow::TryDispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  if (msg->num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_INVALID_ARGS);
    return true;
  }
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  switch (hdr->ordinal) {
    case kControlFlow_Shutdown_Ordinal: {
      auto result = ::fidl::DecodeAs<ShutdownRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->Shutdown(
        Interface::ShutdownCompleter::Sync(txn));
      return true;
    }
    case kControlFlow_NoReplyMustSendAccessDeniedEpitaph_Ordinal: {
      auto result = ::fidl::DecodeAs<NoReplyMustSendAccessDeniedEpitaphRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->NoReplyMustSendAccessDeniedEpitaph(
        Interface::NoReplyMustSendAccessDeniedEpitaphCompleter::Sync(txn));
      return true;
    }
    case kControlFlow_MustSendAccessDeniedEpitaph_Ordinal: {
      auto result = ::fidl::DecodeAs<MustSendAccessDeniedEpitaphRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->MustSendAccessDeniedEpitaph(
        Interface::MustSendAccessDeniedEpitaphCompleter::Sync(txn));
      return true;
    }
    default: {
      return false;
    }
  }
}

bool ControlFlow::Dispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  bool found = TryDispatch(impl, msg, txn);
  if (!found) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_NOT_SUPPORTED);
  }
  return found;
}


void ControlFlow::Interface::MustSendAccessDeniedEpitaphCompleterBase::Reply(int32_t reply) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<MustSendAccessDeniedEpitaphResponse>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _response = *reinterpret_cast<MustSendAccessDeniedEpitaphResponse*>(_write_bytes);
  _response._hdr.ordinal = kControlFlow_MustSendAccessDeniedEpitaph_Ordinal;
  _response.reply = std::move(reply);
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof(MustSendAccessDeniedEpitaphResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<MustSendAccessDeniedEpitaphResponse>(std::move(_response_bytes)));
}

void ControlFlow::Interface::MustSendAccessDeniedEpitaphCompleterBase::Reply(::fidl::BytePart _buffer, int32_t reply) {
  if (_buffer.capacity() < MustSendAccessDeniedEpitaphResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  auto& _response = *reinterpret_cast<MustSendAccessDeniedEpitaphResponse*>(_buffer.data());
  _response._hdr.ordinal = kControlFlow_MustSendAccessDeniedEpitaph_Ordinal;
  _response.reply = std::move(reply);
  _buffer.set_actual(sizeof(MustSendAccessDeniedEpitaphResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<MustSendAccessDeniedEpitaphResponse>(std::move(_buffer)));
}

void ControlFlow::Interface::MustSendAccessDeniedEpitaphCompleterBase::Reply(::fidl::DecodedMessage<MustSendAccessDeniedEpitaphResponse> params) {
  params.message()->_hdr = {};
  params.message()->_hdr.ordinal = kControlFlow_MustSendAccessDeniedEpitaph_Ordinal;
  CompleterBase::SendReply(std::move(params));
}


}  // namespace controlflow
}  // namespace llcpp
}  // namespace test
}  // namespace fidl
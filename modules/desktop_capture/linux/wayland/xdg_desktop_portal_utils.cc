/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "modules/desktop_capture/linux/wayland/xdg_desktop_portal_utils.h"

#include "modules/desktop_capture/linux/wayland/scoped_glib.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace xdg_portal {

std::string RequestResponseToString(RequestResponse request) {
  switch (request) {
    case RequestResponse::kUnknown:
      return "kUnknown";
    case RequestResponse::kSuccess:
      return "kSuccess";
    case RequestResponse::kUserCancelled:
      return "kUserCancelled";
    case RequestResponse::kError:
      return "kError";
    default:
      return "Uknown";
  }
}

void RequestSessionUsingProxy(ScreenCapturePortalInterface* portal,
                              GObject* gobject,
                              GAsyncResult* result) {
  RTC_DCHECK(portal);
  Scoped<GError> error;
  GDBusProxy* proxy = g_dbus_proxy_new_finish(result, error.receive());
  if (!proxy) {
    if (g_error_matches(error.get(), G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;
    RTC_LOG(LS_ERROR) << "Failed to get a proxy for the portal: "
                      << error->message;
    portal->OnPortalDone(RequestResponse::kError);
    return;
  }

  RTC_LOG(LS_INFO) << "Successfully created proxy for the portal.";
  portal->SessionRequest(proxy);
}

void SessionRequestHandler(ScreenCapturePortalInterface* portal,
                           GDBusProxy* proxy,
                           GAsyncResult* result,
                           gpointer user_data) {
  RTC_DCHECK(portal);

  Scoped<GError> error;
  Scoped<GVariant> variant(
      g_dbus_proxy_call_finish(proxy, result, error.receive()));
  if (!variant) {
    if (g_error_matches(error.get(), G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;
    RTC_LOG(LS_ERROR) << "Failed to session: " << error->message;
    portal->OnPortalDone(RequestResponse::kError);
    return;
  }

  RTC_LOG(LS_INFO) << "Initializing the session.";

  Scoped<char> handle;
  g_variant_get_child(variant.get(), /*index=*/0, /*format_string=*/"o",
                      &handle);
  if (!handle) {
    RTC_LOG(LS_ERROR) << "Failed to initialize the session.";
    portal->UnsubscribeSignalHandlers();
    portal->OnPortalDone(RequestResponse::kError);
    return;
  }
}

void SessionRequestResponseSignalHelper(
    const SessionClosedSignalHandler session_close_signal_handler,
    ScreenCapturePortalInterface* portal,
    GDBusConnection* connection,
    std::string& session_handle,
    GVariant* parameters,
    guint& session_closed_signal_id) {
  uint32_t portal_response;
  Scoped<GVariant> response_data;
  g_variant_get(parameters, /*format_string=*/"(u@a{sv})", &portal_response,
                response_data.receive());
  Scoped<GVariant> g_session_handle(
      g_variant_lookup_value(response_data.get(), /*key=*/"session_handle",
                             /*expected_type=*/nullptr));
  session_handle = g_variant_dup_string(
      /*value=*/g_session_handle.get(), /*length=*/nullptr);

  if (session_handle.empty() || portal_response) {
    RTC_LOG(LS_ERROR) << "Failed to request the session subscription.";
    portal->OnPortalDone(RequestResponse::kError);
    return;
  }

  session_closed_signal_id = g_dbus_connection_signal_subscribe(
      connection, kDesktopBusName, kSessionInterfaceName, /*member=*/"Closed",
      session_handle.c_str(), /*arg0=*/nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
      session_close_signal_handler, portal, /*user_data_free_func=*/nullptr);
}

void StartRequestedHandler(ScreenCapturePortalInterface* portal,
                           GDBusProxy* proxy,
                           GAsyncResult* result) {
  RTC_DCHECK(portal);
  Scoped<GError> error;
  Scoped<GVariant> variant(
      g_dbus_proxy_call_finish(proxy, result, error.receive()));
  if (!variant) {
    if (g_error_matches(error.get(), G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;
    RTC_LOG(LS_ERROR) << "Failed to start the portal session: "
                      << error->message;
    portal->OnPortalDone(RequestResponse::kError);
    return;
  }

  Scoped<char> handle;
  g_variant_get_child(variant.get(), 0, "o", handle.receive());
  if (!handle) {
    RTC_LOG(LS_ERROR) << "Failed to initialize the start portal session.";
    portal->UnsubscribeSignalHandlers();
    portal->OnPortalDone(RequestResponse::kError);
    return;
  }

  RTC_LOG(LS_INFO) << "Subscribed to the start signal.";
}

std::string PrepareSignalHandle(const char* token,
                                GDBusConnection* connection) {
  Scoped<char> sender(
      g_strdup(g_dbus_connection_get_unique_name(connection) + 1));
  for (int i = 0; sender.get()[i]; ++i) {
    if (sender.get()[i] == '.') {
      sender.get()[i] = '_';
    }
  }
  const char* handle = g_strconcat(kDesktopRequestObjectPath, "/", sender.get(),
                                   "/", token, /*end of varargs*/ nullptr);
  return handle;
}

uint32_t SetupRequestResponseSignal(const char* object_path,
                                    const GDBusSignalCallback callback,
                                    gpointer user_data,
                                    GDBusConnection* connection) {
  return g_dbus_connection_signal_subscribe(
      connection, kDesktopBusName, kRequestInterfaceName, "Response",
      object_path, /*arg0=*/nullptr, G_DBUS_SIGNAL_FLAGS_NO_MATCH_RULE,
      callback, user_data, /*user_data_free_func=*/nullptr);
}

void RequestSessionProxy(const char* interface_name,
                         const ProxyRequestCallback proxy_request_callback,
                         GCancellable* cancellable,
                         gpointer user_data) {
  g_dbus_proxy_new_for_bus(
      G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, /*info=*/nullptr,
      kDesktopBusName, kDesktopObjectPath, interface_name, cancellable,
      reinterpret_cast<GAsyncReadyCallback>(proxy_request_callback), user_data);
}

void SetupSessionRequestHandlers(
    const std::string& portal_prefix,
    const SessionRequestCallback session_request_callback,
    const SessionRequestResponseSignalHandler request_response_signale_handler,
    GDBusConnection* connection,
    GDBusProxy* proxy,
    GCancellable* cancellable,
    std::string& portal_handle,
    guint& session_request_signal_id,
    gpointer user_data) {
  GVariantBuilder builder;
  Scoped<char> variant_string;

  g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
  variant_string = g_strdup_printf("%s_session%d", portal_prefix.c_str(),
                                   g_random_int_range(0, G_MAXINT));
  g_variant_builder_add(&builder, "{sv}", "session_handle_token",
                        g_variant_new_string(variant_string.get()));

  variant_string = g_strdup_printf("%s_%d", portal_prefix.c_str(),
                                   g_random_int_range(0, G_MAXINT));
  g_variant_builder_add(&builder, "{sv}", "handle_token",
                        g_variant_new_string(variant_string.get()));

  portal_handle = PrepareSignalHandle(variant_string.get(), connection);
  session_request_signal_id = SetupRequestResponseSignal(
      portal_handle.c_str(), request_response_signale_handler, user_data,
      connection);

  RTC_LOG(LS_INFO) << "Desktop session requested.";
  g_dbus_proxy_call(
      proxy, "CreateSession", g_variant_new("(a{sv})", &builder),
      G_DBUS_CALL_FLAGS_NONE, /*timeout=*/-1, cancellable,
      reinterpret_cast<GAsyncReadyCallback>(session_request_callback),
      user_data);
}

void StartSessionRequest(
    const std::string& prefix,
    const std::string session_handle,
    const StartRequestResponseSignalHandler signal_handler,
    const SessionStartRequestedHandler session_started_handler,
    GDBusProxy* proxy,
    GDBusConnection* connection,
    GCancellable* cancellable,
    guint& start_request_signal_id,
    std::string& start_handle,
    gpointer user_data) {
  GVariantBuilder builder;
  Scoped<char> variant_string;

  g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
  variant_string =
      g_strdup_printf("%s%d", prefix.c_str(), g_random_int_range(0, G_MAXINT));
  g_variant_builder_add(&builder, "{sv}", "handle_token",
                        g_variant_new_string(variant_string.get()));

  start_handle = PrepareSignalHandle(variant_string.get(), connection);
  start_request_signal_id = SetupRequestResponseSignal(
      start_handle.c_str(), signal_handler, user_data, connection);

  // "Identifier for the application window", this is Wayland, so not "x11:...".
  const char parent_window[] = "";

  RTC_LOG(LS_INFO) << "Starting the portal session.";
  g_dbus_proxy_call(
      proxy, "Start",
      g_variant_new("(osa{sv})", session_handle.c_str(), parent_window,
                    &builder),
      G_DBUS_CALL_FLAGS_NONE, /*timeout=*/-1, cancellable,
      reinterpret_cast<GAsyncReadyCallback>(session_started_handler),
      user_data);
}

void TearDownSession(std::string session_handle,
                     GDBusProxy* proxy,
                     GCancellable* cancellable,
                     GDBusConnection* connection) {
  if (!session_handle.empty()) {
    Scoped<GDBusMessage> message(
        g_dbus_message_new_method_call(kDesktopBusName, session_handle.c_str(),
                                       kSessionInterfaceName, "Close"));
    if (message.get()) {
      Scoped<GError> error;
      g_dbus_connection_send_message(connection, message.get(),
                                     G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                     /*out_serial=*/nullptr, error.receive());
      if (error.get()) {
        RTC_LOG(LS_ERROR) << "Failed to close the session: " << error->message;
      }
    }
  }

  if (cancellable) {
    g_cancellable_cancel(cancellable);
    g_object_unref(cancellable);
  }

  if (proxy) {
    g_object_unref(proxy);
  }
}

}  // namespace xdg_portal
}  // namespace webrtc

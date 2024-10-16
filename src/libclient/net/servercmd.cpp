// SPDX-License-Identifier: GPL-3.0-or-later

#include "libclient/net/servercmd.h"
#include "libshared/util/qtcompat.h"

namespace net {

drawdance::Message ServerCommand::make(const QString &cmd, const QJsonArray &args, const QJsonObject &kwargs)
{
	return ServerCommand { cmd, args, kwargs }.toMessage();
}

drawdance::Message ServerCommand::makeKick(int target, bool ban)
{
	Q_ASSERT(target>0 && target<256);
	QJsonObject kwargs;
	if(ban)
		kwargs["ban"] = true;

	return make("kick-user", QJsonArray() << target, kwargs);
}

drawdance::Message ServerCommand::makeAnnounce(const QString &url, bool privateMode)
{
	QJsonObject kwargs;
	if(privateMode)
		kwargs["private"] = true;

	return make("announce-session", QJsonArray() << url, kwargs);
}

drawdance::Message ServerCommand::makeUnannounce(const QString &url)
{
	return make("unlist-session", QJsonArray() << url);
}

drawdance::Message ServerCommand::makeUnban(int entryId)
{
	return make("remove-ban", QJsonArray() << entryId);
}

drawdance::Message ServerCommand::makeMute(int target, bool mute)
{
	return make("mute", QJsonArray() << target << mute);
}

drawdance::Message ServerCommand::toMessage() const
{
	QJsonObject o;
	o["cmd"] = cmd;
	if(!args.isEmpty()) {
		o["args"] = args;
	}
	if(!kwargs.isEmpty()) {
		o["kwargs"] = kwargs;
	}
	return drawdance::Message::makeServerCommand(0, QJsonDocument{o});
}

static ServerReply ServerReplyFromJson(const QJsonDocument &doc)
{
	const QJsonObject o = doc.object();
	const QString typestr = o.value("type").toString();

	ServerReply r;

	if(typestr == "login")
		r.type = ServerReply::ReplyType::Login;
	else if(typestr == "msg")
		r.type = ServerReply::ReplyType::Message;
	else if(typestr == "alert")
		r.type = ServerReply::ReplyType::Alert;
	else if(typestr == "error")
		r.type = ServerReply::ReplyType::Error;
	else if(typestr == "result")
		r.type = ServerReply::ReplyType::Result;
	else if(typestr == "log")
		r.type = ServerReply::ReplyType::Log;
	else if(typestr == "sessionconf")
		r.type = ServerReply::ReplyType::SessionConf;
	else if(typestr == "sizelimit")
		r.type = ServerReply::ReplyType::SizeLimitWarning;
	else if(typestr == "status")
		r.type = ServerReply::ReplyType::Status;
	else if(typestr == "reset")
		r.type = ServerReply::ReplyType::Reset;
	else if(typestr == "autoreset")
		r.type = ServerReply::ReplyType::ResetRequest;
	else if(typestr == "catchup")
		r.type = ServerReply::ReplyType::Catchup;
	else
		r.type = ServerReply::ReplyType::Unknown;

	r.message = o.value("message").toString();
	r.reply = o;
	return r;
}

ServerReply ServerReply::fromMessage(const drawdance::Message &msg)
{
	if(msg.isNull() || msg.type() != DP_MSG_SERVER_COMMAND) {
		qWarning("ServerReply::fromMessage: bad message");
		return ServerReply{
			ServerReply::ReplyType::Unknown,
			QString(),
			QJsonObject()
		};
	}

	size_t len;
	const char *data = DP_msg_server_command_msg(msg.toServerCommand(), &len);
	QByteArray bytes = QByteArray::fromRawData(data, compat::castSize(len));

	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
	if(err.error != QJsonParseError::NoError) {
		qWarning("ServerReply::fromMessage JSON parsing error: %s", qPrintable(err.errorString()));
		return ServerReply{
			ServerReply::ReplyType::Unknown,
			QString(),
			QJsonObject()
		};
	}

	return ServerReplyFromJson(doc);
}

}

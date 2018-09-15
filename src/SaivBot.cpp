//SaivBot.cpp
//Author: Sivert Andresen Cubedo

#include "SaivBot.hpp"

SaivBot::SaivBot(boost::asio::io_context & ioc) :
	m_ioc(ioc),
	m_resolver(ioc),
	m_sock(ioc)
{
}

void SaivBot::loadConfig(const std::filesystem::path & path)
{
	nlohmann::json j;
	std::fstream fs(path, std::ios::in);
	if (!fs.is_open()) throw std::runtime_error("Can't open config file");
	fs >> j;
	m_host = j["host"];
	m_port = j["port"];
	m_nick = j["nick"];
	m_password = j["password"];
	nlohmann::from_json(j["modlist"], m_modlist);
	nlohmann::from_json(j["whitelist"], m_whitelist);
}

void SaivBot::saveConfig(const std::filesystem::path & path)
{
	nlohmann::json j;
	j["host"] = m_host;
	j["port"] = m_port;
	j["nick"] = m_nick;
	j["password"] = m_password;
	j["modlist"] = m_modlist;
	j["whitelist"] = m_whitelist;
	std::fstream fs(path, std::ios::trunc | std::ios::out);
	if (!fs.is_open()) throw std::runtime_error("Can't open config file");
	fs << j;
}

void SaivBot::run()
{
	m_running = true;

	m_resolver.async_resolve(
		m_host,
		m_port,
		std::bind(
			&SaivBot::resolveHandler,
			this,
			std::placeholders::_1,
			std::placeholders::_2
		)
	);
}

void SaivBot::resolveHandler(boost::system::error_code ec, boost::asio::ip::tcp::resolver::results_type results)
{

	boost::asio::async_connect(
		m_sock,
		results.begin(),
		results.end(),
		std::bind(
			&SaivBot::connectHandler,
			this,
			std::placeholders::_1
		)
	);
	if (ec) throw std::runtime_error(ec.message());
}

void SaivBot::connectHandler(boost::system::error_code ec)
{
	sendIRC("PASS " + m_password);

	sendIRC("NICK " + m_nick);

	std::string channel("#"); 
	channel.append(m_nick);
	std::transform(channel.begin(), channel.end(), channel.begin(), ::tolower);

	sendJOIN(channel);
	sendPRIVMSG(channel, "monkaMEGA");

	m_sock.async_read_some(
		boost::asio::buffer(m_recv_buffer),
		std::bind(
			&SaivBot::receiveHandler,
			this,
			std::placeholders::_1,
			std::placeholders::_2
		)
	);
	if (ec) throw std::runtime_error(ec.message());
}

void SaivBot::receiveHandler(boost::system::error_code ec, std::size_t ret)
{
	m_buffer.append(m_recv_buffer.data(), ret);
	
	parseBuffer();
	consumeMsgBuffer();

	if (!m_running) return;

	m_sock.async_read_some(
		boost::asio::buffer(m_recv_buffer),
		std::bind(
			&SaivBot::receiveHandler,
			this,
			std::placeholders::_1,
			std::placeholders::_2
		)
	);
	if (ec) throw std::runtime_error(ec.message());
}

void SaivBot::sendIRC(const std::string & msg)
{
	const std::string cr("\r\n");
	
	{
		std::lock_guard<std::mutex> lock(m_send_mutex);
		
		std::shared_ptr<std::string> msg_ptr = std::make_shared<std::string>(msg);
		
		if (*msg_ptr == m_last_message_queued) {
			msg_ptr->append("\x20\xe2\x81\xad");
		}

		m_last_message_queued = *msg_ptr;

		msg_ptr->append(cr);


		auto now = std::chrono::system_clock::now();

		if (m_next_message_time < now) {
			m_next_message_time = now + std::chrono::milliseconds(1500);
			boost::asio::async_write(
				m_sock,
				boost::asio::buffer(*msg_ptr),
				std::bind(
					&SaivBot::sendHandler,
					this,
					std::placeholders::_1,
					std::placeholders::_2,
					msg_ptr
				)
			);
		}
		else {
			auto timer_ptr = std::make_shared<boost::asio::system_timer>(m_ioc);
			timer_ptr->expires_at(m_next_message_time);

			m_next_message_time += std::chrono::milliseconds(1500);

			timer_ptr->async_wait(
				std::bind(
					&SaivBot::sendTimerHandler,
					this,
					std::placeholders::_1,
					timer_ptr,
					msg_ptr
				)
			);
		}
	}
}

void SaivBot::sendTimerHandler(boost::system::error_code ec, std::shared_ptr<boost::asio::system_timer> timer_ptr, std::shared_ptr<std::string> ptr)
{

	boost::asio::async_write(
		m_sock,
		boost::asio::buffer(*ptr),
		std::bind(
			&SaivBot::sendHandler,
			this,
			std::placeholders::_1,
			std::placeholders::_2,
			ptr
		)
	);
	if (ec) throw std::runtime_error(ec.message());
}

void SaivBot::sendHandler(boost::system::error_code ec, std::size_t ret, std::shared_ptr<std::string> ptr)
{
	if (ec) throw std::runtime_error(ec.message());
}

void SaivBot::parseBuffer()
{
	const std::string cr("\r\n");
	while (true) {
		std::size_t n = m_buffer.find_first_of(cr);
		if (n == m_buffer.npos) return;
		std::string line = m_buffer.substr(0, n);
		m_buffer.erase(m_buffer.begin(), m_buffer.begin() + n + cr.size());
		m_msg_buffer.emplace_back(std::move(line));
	}
}


void SaivBot::consumeMsgBuffer()
{
	while (!m_msg_buffer.empty()) {
		auto & msg = m_msg_buffer.front();
		
		if (msg.getCommand() != "PRIVMSG") {
			if (msg.getCommand() == "PING") {
				sendIRC("PONG");
			}
		}
		else {
			auto body_vec = splitString(std::string(msg.getBody()));
			if (body_vec.size() >= 2 && caselessCompare(body_vec[0], m_nick)) {
				auto command_it = std::find_if(
					m_command_containers.begin(),
					m_command_containers.end(),
					[&](auto & cc) {return cc.m_command == body_vec[1]; }
				);
				if (command_it != m_command_containers.end()) {
					command_it->m_func(msg, body_vec.cbegin() + 1, body_vec.cend());
				}
			}
		}
		std::cout << msg.getData() << "\n";
		m_msg_buffer.pop_front();
	}
}

void SaivBot::sendPRIVMSG(const std::string_view & channel, const std::string_view & msg)
{
	//std::stringstream ss;
	//ss << "PRIVMSG " << channel << " :" << msg;
	//sendIRC(ss.str());
	sendIRC(std::string("PRIVMSG ").append(channel).append(" :").append(msg));
}

void SaivBot::sendJOIN(const std::string_view & channel)
{
	sendIRC(std::string("JOIN ").append(channel));
}

void SaivBot::sendPART(const std::string_view & channel)
{
	sendIRC(std::string("PART ").append(channel));
}

bool SaivBot::isModerator(const std::string_view & user)
{
	std::string str;
	std::transform(user.begin(), user.end(), std::back_inserter(str), ::tolower);
	return m_modlist.find(str) != m_modlist.end();
}

bool SaivBot::isWhitelisted(const std::string_view & user)
{
	std::string str;
	std::transform(user.begin(), user.end(), std::back_inserter(str), ::tolower);
	return m_whitelist.find(str) != m_whitelist.end();
}

void SaivBot::countCommandCallback(LogDownloader::LogContainer && logs, std::shared_ptr<std::string> search_ptr, std::shared_ptr<std::string> channel_ptr, std::shared_ptr<std::string> nick_ptr)
{
	std::stringstream reply;
	//if (in_log.getData() == "{\"message\":\"Not Found\"}") {
	//	reply << *nick_ptr << ", Log not found NaM";
	//	sendPRIVMSG(*channel_ptr, reply.str());
	//}
	//else {
	std::size_t count = 0;
	auto searcher = std::boyer_moore_searcher(search_ptr->begin(), search_ptr->end());
	for (auto & log : logs) {
		std::cout << log.getSource() << "\n";
		if (log.getData() == "{\"message\":\"Not Found\"}") continue;
		for (auto row : log.getLines()) {
			count += countTargetOccurrences(std::get<2>(row).begin(), std::get<2>(row).end(), searcher);
		}
	}
	//reply << *nick_ptr << ", count: " << count << " source: " << in_log.getSource();
	reply << *nick_ptr << ", count: " << count;
	sendPRIVMSG(*channel_ptr, reply.str());
	//}
}

std::size_t countTargetOccurrences(const std::string_view & str, const std::string_view & target)
{
	auto search = std::boyer_moore_searcher(target.begin(), target.end());
	return countTargetOccurrences(str.begin(), str.end(), search);
}

bool SaivBot::caselessCompare(const std::string_view & str1, const std::string_view & str2)
{
	if (str1.size() != str2.size()) {
		return false;
	}
	for (std::size_t i = 0; i < str1.size(); ++i) {
		if (std::tolower(str1[i]) != std::tolower(str2[i])) {
			return false;
		}
	}
	return true;
}

void SaivBot::shutdownCommandFunc(const IRCMessage & msg, CommandContainer::IteratorType begin, CommandContainer::IteratorType end)
{
	if (isModerator(msg.getNick())) {
		m_running = false;
	}
}

void SaivBot::helpCommandFunc(const IRCMessage & msg, CommandContainer::IteratorType begin, CommandContainer::IteratorType end)
{
	if (isWhitelisted(msg.getNick())) {
		OptionParser parser(Option(m_command_containers[Commands::help_command].m_command, 1));
		auto set = parser.parse(begin, end);
		if (auto r = set.find(parser.get<0>())) {
			auto it = std::find_if(m_command_containers.begin(), m_command_containers.end(), [&](auto & con) {return caselessCompare(con.m_command, r->get()[0]); });
			if (it != m_command_containers.end()) {
				std::stringstream reply;
				reply << msg.getUser() << ", " << it->m_description << " Usage: " << it->m_command << " " << it->m_arguments;
				sendPRIVMSG(msg.getParams()[0], reply.str());
			}
		}
	}
}

void SaivBot::countCommandFunc(const IRCMessage & msg, CommandContainer::IteratorType begin, CommandContainer::IteratorType end)
{
	if (isWhitelisted(msg.getNick())) {
		OptionParser parser(Option(m_command_containers[Commands::count_command].m_command, 1), Option("-channel", 1), Option("-user", 1), Option("-year", 1), Option("-month", 1));

		auto set = parser.parse(begin, end);

		std::shared_ptr<std::string> search_ptr;
		std::string channel(msg.getParams()[0]);
		std::string user(msg.getNick());
		std::string year = "2018";
		std::string month = "September";

		if (auto r = set.find(parser.get<0>())) {
			search_ptr = std::make_shared<std::string>(r->get()[0]);
		}
		else {
			return;
		}

		if (auto r = set.find(parser.get<1>())) {
			channel = r->get()[0];
		}
		if (auto r = set.find(parser.get<2>())) {
			user = r->get()[0];
		}
		if (auto r = set.find(parser.get<3>())) {
			year = r->get()[0];
		}
		if (auto r = set.find(parser.get<4>())) {
			month = r->get()[0];
		}

		if (channel[0] == '#') channel.erase(0, 1);

		std::stringstream target_ss;
		target_ss << "/channel/" << channel << "/user/" << user << "/" << year << "/" << month;

		const std::string host("api.gempir.com");
		const std::string target = target_ss.str();
		const std::string port("443");

		auto msg_ptr = std::make_shared<IRCMessage>(msg);

		std::make_shared<LogDownloader>(m_ioc)->run(
			std::bind(
				&SaivBot::countCommandCallback,
				this,
				std::placeholders::_1,
				search_ptr,
				std::make_shared<std::string>(msg.getParams()[0]),
				std::make_shared<std::string>(msg.getNick())
			),
			host,
			port,
			std::move( std::vector<std::string>({ target } ))
		);
	}
}

void SaivBot::promoteCommandFunc(const IRCMessage & msg, CommandContainer::IteratorType begin, CommandContainer::IteratorType end)
{
	if (isModerator(msg.getNick())) {
		OptionParser parser(Option(m_command_containers[Commands::promote_command].m_command, 1));
		auto set = parser.parse(begin, end);
		if (auto r = set.find(parser.get<0>())) {
			std::string user;
			std::transform(r->get()[0].begin(), r->get()[0].end(), std::back_inserter(user), ::tolower);
			if (m_whitelist.find(user) == m_whitelist.end()) {
				m_whitelist.emplace(user);
				sendPRIVMSG(msg.getParams()[0], user + " promoted");
			}
		}
	}
}

void SaivBot::demoteCommandFunc(const IRCMessage & msg, CommandContainer::IteratorType begin, CommandContainer::IteratorType end)
{
	if (isModerator(msg.getNick())) {
		OptionParser parser(Option(m_command_containers[Commands::demote_command].m_command, 1));
		auto set = parser.parse(begin, end);
		if (auto r = set.find(parser.get<0>())) {
			std::string user;
			std::transform(r->get()[0].begin(), r->get()[0].end(), std::back_inserter(user), ::tolower);
			auto it = m_whitelist.find(user);
			if (it != m_whitelist.end()) {
				m_whitelist.erase(it);
				sendPRIVMSG(msg.getParams()[0], user + " demoted");
			}
		}
	}
}

void SaivBot::joinCommandFunc(const IRCMessage & msg, CommandContainer::IteratorType begin, CommandContainer::IteratorType end)
{
	if (isModerator(msg.getNick())) {
		OptionParser parser(Option(m_command_containers[Commands::join_command].m_command, 1));
		auto set = parser.parse(begin, end);
		if (auto r = set.find(parser.get<0>())) {
			std::string channel("#");
			channel.append(r->get()[0]);
			std::transform(channel.begin(), channel.end(), channel.begin(), ::tolower);
			sendJOIN(channel);
			sendPRIVMSG(channel, "monkaMEGA");
		}
	}
}

void SaivBot::partCommandFunc(const IRCMessage & msg, CommandContainer::IteratorType begin, CommandContainer::IteratorType end)
{
	if (isModerator(msg.getNick())) {
		OptionParser parser(Option(m_command_containers[Commands::part_command].m_command, 1));
		auto set = parser.parse(begin, end);
		if (auto r = set.find(parser.get<0>())) {
			std::string channel("#");
			channel.append(r->get()[0]);
			std::transform(channel.begin(), channel.end(), channel.begin(), ::tolower);
			sendPART(channel);
		}
	}
}

void IRCMessage::parse()
{
	std::string_view data_view(m_data);
	std::size_t space = 0;

	//prefix
	if (data_view[0] == ':') {
		space = data_view.find_first_of(' ');
		std::string_view prefix_view = data_view.substr(1, space);
		
		std::size_t at = prefix_view.find_first_of('@');
		if (at != prefix_view.npos) {
			std::size_t ex = prefix_view.find_first_of('!');
			if (ex != prefix_view.npos) {
				m_nick_view = prefix_view.substr(0, ex);
				m_user_view = prefix_view.substr(ex + 1, at - ex - 1);
				m_host_view = prefix_view.substr(at + 1);
			}
			else {
				m_nick_view = prefix_view.substr(0, at);
				m_host_view = prefix_view.substr(at + 1);
			}
		}
		else {
			m_nick_view = prefix_view;
		}
		
		data_view.remove_prefix(space + 1);
		if (data_view.empty()) return;
	}
	
	//command
	{
		space = data_view.find_first_of(' ');
		m_command_view = data_view.substr(0, space);
		
		data_view.remove_prefix(space + 1);
		if (data_view.empty()) return;
	}

	//params
	{
		while (!data_view.empty()) {
			if (data_view[0] == ':') break;
			space = data_view.find_first_of(' ');
			if (space == data_view.npos) {
				m_params_vec.push_back(data_view);
				data_view.remove_prefix(data_view.size());
				break;
			}
			else {
				m_params_vec.push_back(data_view.substr(0, space));
				data_view.remove_prefix(space + 1);
			}
		}
		if (data_view.empty()) return;
	}

	//body
	if (data_view[0] == ':') {
		m_body_view = data_view.substr(1);
	}
}

void IRCMessage::print(std::ostream & stream)
{
	stream
		<< "nick: " << m_nick_view << "\n"
		<< "user: " << m_user_view << "\n"
		<< "host: " << m_host_view << "\n"
		<< "command: " << m_command_view << "\n"
		<< "params: " << [](auto & vec)->std::string {std::string str; std::for_each(vec.begin(), vec.end(), [&](auto & s) {str.append(std::string(s) + ", "); }); return str; }(m_params_vec) << "\n"
		<< "body: " << m_body_view << "\n";
}

const std::string & IRCMessage::getData() const
{
	return m_data;
}

const std::string_view & IRCMessage::getNick() const
{
	return m_nick_view;
}

const std::string_view & IRCMessage::getUser() const
{
	return m_user_view;
}

const std::string_view & IRCMessage::getHost() const
{
	return m_host_view;
}

const std::string_view & IRCMessage::getCommand() const
{
	return m_command_view;
}

const std::vector<std::string_view>& IRCMessage::getParams() const
{
	return m_params_vec;
}

const std::string_view & IRCMessage::getBody() const
{
	return m_body_view;
}

std::vector<std::string> splitString(const std::string & str)
{
	std::istringstream iss(str);
	return { std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{} };
}
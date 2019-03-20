//Log.hpp
#pragma once
#ifndef Log_HEADER
#define Log_HEADER

//C++
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <set>
#include <unordered_map>
#include <cstdint>

//Boost
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/serialization/binary_object.hpp>
#include <boost/functional/hash.hpp>

//local
#include "TimeDetail.hpp"

namespace Log
{
	class LineView
	{
	public:
		LineView(
			TimeDetail::TimePoint time,
			std::string_view name_view,
			std::string_view message_view
		);

		TimeDetail::TimePoint getTime() const;

		std::string_view getNameView() const;

		std::string_view getMessageView() const;

		static LineView copyView(const char* old_offset, const char* new_offset, LineView source);

		bool isEqual(const LineView & other) const noexcept
		{
			return (m_time == other.m_time && m_name_view == other.m_name_view && m_message_view == other.m_message_view);
		}

		friend bool operator==(const LineView & lhs, const LineView & rhs) noexcept
		{
			return lhs.isEqual(rhs);
		}

		friend bool operator!=(const LineView & lhs, const LineView & rhs) noexcept
		{
			return !(lhs == rhs);
		}

	private:
		TimeDetail::TimePoint m_time;
		std::string_view m_name_view;
		std::string_view m_message_view;
	};

	class Line
	{
	public:
		Line(const LineView & line_view);

		Line(const Line & source);
		
		Line & operator=(const Line & source)
		{
			copyLineImpl(source);
			return *this;
		}

		Line(Line && source);

		Line & operator=(Line && source)
		{
			moveLineImpl(std::move(source));
			return *this;
		}

		std::string & getData();

		TimeDetail::TimePoint getTime() const;
		
		std::string_view getNameView() const;
		
		std::string_view getMessageView() const;
	private:
		std::string m_data;
		TimeDetail::TimePoint m_time;
		std::string_view m_name_view;
		std::string_view m_message_view;

		void copyLineViewImpl(const LineView & line_view);

		void copyLineImpl(const Line & source);

		void moveLineImpl(Line && source);
	};

	//using ParserFunc = std::function<bool(const std::string_view, std::vector<std::string_view> & names, std::vector<LineView> & lines)>;
	using ChannelName = std::string;

	class log_identifier
	{
	public:
		log_identifier() = default;

		log_identifier(ChannelName && channel_name, TimeDetail::TimePeriod && period) noexcept :
			m_channel_name(std::forward<ChannelName>(channel_name)),
			m_period(std::forward<TimeDetail::TimePeriod>(period))
		{
		}

		log_identifier(ChannelName && channel_name, TimeDetail::TimePeriod && period, std::string && user_log) noexcept :
			m_channel_name(std::forward<ChannelName>(channel_name)),
			m_period(std::forward<TimeDetail::TimePeriod>(period)),
			m_user_log(std::forward<std::string>(user_log))
		{
		}

		const ChannelName & get_channel_name() const noexcept
		{
			return m_channel_name;
		}

		const TimeDetail::TimePeriod & get_period() const noexcept
		{
			return m_period;
		}

		const std::string & get_userlog_name() const noexcept
		{
			return m_user_log;
		}

		bool is_channel_log() const noexcept
		{
			return m_user_log.empty();
		}

		bool is_user_log() const noexcept
		{
			return !m_user_log.empty();
		}

		friend bool operator==(const log_identifier & rhs, const log_identifier & lhs) noexcept
		{
			return (rhs.m_channel_name == lhs.m_channel_name && rhs.m_period == lhs.m_period && rhs.m_user_log == lhs.m_user_log);
		}

		friend bool operator!=(const log_identifier & rhs, const log_identifier & lhs) noexcept
		{
			return !(rhs == lhs);
		}

		struct hash
		{
			std::size_t operator()(const log_identifier & id) noexcept
			{
				std::size_t seed = 0;
				boost::hash_combine(seed, id.m_channel_name);
				boost::hash_combine(seed, TimeDetail::TimePeriod::hash()(id.m_period));
				boost::hash_combine(seed, id.m_user_log);
				return seed;
			}
		};
	private:
		ChannelName m_channel_name;
		TimeDetail::TimePeriod m_period;
		std::string m_user_log;	//if not empty, then it is a userlog
	};

	class Log;
	using log_parser_func = std::function<std::optional<typename Log>(log_identifier&&, std::vector<char>&&)>;

	class Log
	{
	public:
		Log(log_identifier && id, std::vector<char> && data, std::vector<std::string_view> && names, std::vector<LineView> && lines) :
			m_id(std::move(id)),
			m_data(std::move(data)),
			m_names(std::move(names)),
			m_lines(std::move(lines))
		{
		}

		Log(const Log & source) = delete;
		Log & operator=(const Log & source) = delete;

		Log(Log && source)
		{
			moveImpl(std::move(source));
		}
		Log & operator=(Log && source)
		{
			moveImpl(std::move(source));
			return *this;
		}

		const log_identifier & get_id() const noexcept
		{
			return m_id;
		}

		const std::vector<char> & getData() const;

		const std::vector<std::string_view> & getNames() const;

		const std::vector<LineView> & getLines() const;
		
		bool isEqual(const Log & other) const noexcept
		{
			if (m_lines.size() != other.m_lines.size()) return false;
			if (m_id != other.m_id) return false;
			for (std::size_t i = 0; i < m_lines.size(); ++i) {
				if (m_lines[i] != other.m_lines[i]) {
					return false;
				}
			}
			return true;
		}

		friend bool operator==(const Log & lhs, const Log & rhs) noexcept
		{
			return lhs.isEqual(rhs);
		}

		friend bool operator!=(const Log & lhs, const Log & rhs) noexcept
		{
			return !(lhs == rhs);
		}

	private:
		log_identifier m_id;
		std::vector<char> m_data;
		std::vector<std::string_view> m_names; //sorted set of names (no duplicates)
		std::vector<LineView> m_lines;

		void moveImpl(Log && source);
	};

	namespace Impl
	{
		template<class ForwardIt, class T, class Compare = std::less<>>
		ForwardIt binary_find(ForwardIt first, ForwardIt last, const T& value, Compare comp = {})
		{
			first = std::lower_bound(first, last, value, comp);
			return first != last && !comp(value, *first) ? first : last;
		}

		inline bool isBigEndian()
		{
			union {
				std::uint32_t i;
				char c[4];
			} num = { 0x01020304 };
			return num.c[0] == 1;
		}

		template <typename ByteFieldType, typename FileOffsetType, typename SizeType, typename TimePeriodType>
		struct LogFileHeader
		{
			ByteFieldType format_mode;
			ByteFieldType endianness;
			FileOffsetType channel_name_offset;
			FileOffsetType userlog_name_offset;
			FileOffsetType username_data_offset;
			FileOffsetType message_data_offset;
			FileOffsetType username_set_offset;
			FileOffsetType timepoint_list_offset;
			FileOffsetType username_list_offset;
			FileOffsetType message_list_offset;
			SizeType number_of_lines;
			TimePeriodType time_period;
		};

		template <typename ByteFieldType, typename DataOffsetType, typename SizeType, class OutStream, typename FileOffsetType = std::uint64_t, typename TimePointType = TimeDetail::TimePoint, typename TimePeriodType = TimeDetail::TimePeriod>
		void serialize(OutStream & out_stream, const Log & log, ByteFieldType format_mode, ByteFieldType endianness)
		{
			using HeaderType = LogFileHeader<ByteFieldType, FileOffsetType, SizeType, TimePeriodType>;
			
			auto write_type_func = [&](auto type) { out_stream.write(reinterpret_cast<char*>(&type), sizeof(type)); };
			auto write_padding_func = [&](std::size_t amount) { for (std::size_t i = 0; i < amount; ++i) out_stream.put('\0'); };
			auto write_view_func = [&](std::string_view view) {out_stream.write(view.data(), view.size()); };

			FileOffsetType current_write_offset = 0;
			auto assign_offset_func = [&](SizeType size) -> FileOffsetType {if (size == 0) return 0; FileOffsetType old = current_write_offset; current_write_offset += size; return old; };

			const SizeType username_set_element_size = sizeof(DataOffsetType) + sizeof(SizeType);
			const SizeType timepoint_list_element_size = sizeof(TimePointType);
			const SizeType username_list_element_size = sizeof(DataOffsetType);
			const SizeType message_list_element_size = sizeof(DataOffsetType) + sizeof(SizeType);

			HeaderType header;

			header.format_mode = format_mode;
			header.endianness = endianness;

			if (format_mode == 1) { //16-bit
				header.channel_name_offset = 0x5A;
			}
			else if (format_mode == 2) { //32-bit
				header.channel_name_offset = 0x5C;
			}
			else if (format_mode == 3) { //64-bit
				header.channel_name_offset = 0x60;
			}

			SizeType channel_name_size = static_cast<SizeType>(log.get_id().get_channel_name().size());
			SizeType userlog_name_size = static_cast<SizeType>(log.get_id().get_userlog_name().size());
			SizeType username_data_size = 0;
			SizeType message_data_size = 0;
			SizeType username_set_size = 0;
			SizeType number_of_lines = 0;

			std::vector<std::pair<DataOffsetType, SizeType>> username_set;
			std::vector<DataOffsetType> username_list;
			{
				//calculate offsets
				username_set.reserve(log.getNames().size());
				for (auto & name : log.getNames()) {
					username_set.emplace_back(username_data_size, static_cast<SizeType>(name.size()));
					username_data_size += static_cast<SizeType>(name.size());
				}
				username_set_size = static_cast<SizeType>(username_set.size());

				username_list.reserve(log.getLines().size());
				//assign indexes
				for (auto & line : log.getLines()) {
					auto it = binary_find(
						log.getNames().begin(),
						log.getNames().end(),
						line.getNameView()
					);
					assert(it != log.getNames().end());
					DataOffsetType index = static_cast<DataOffsetType>(std::distance(log.getNames().begin(), it));
					username_list.push_back(index);
					assert(index < username_set.size());
				}
			}
			
			std::vector<std::pair<DataOffsetType, SizeType>> message_list;
			{
				message_list.reserve(log.getLines().size());
				for (auto & line : log.getLines()) {
					message_list.emplace_back(message_data_size, static_cast<SizeType>(line.getMessageView().size()));
					message_data_size += static_cast<SizeType>(line.getMessageView().size());
				}
			}

			//Arrange offsets
			header.username_data_offset = assign_offset_func(username_data_size);

			if (userlog_name_size > 0) {
				header.userlog_name_offset = header.channel_name_offset + sizeof(SizeType) + channel_name_size;
			}
			else {
				header.userlog_name_offset = 0;
			}
			if (channel_name_size > 0) {
				header.username_data_offset = header.channel_name_offset + sizeof(SizeType) + channel_name_size;
			}
			else {
				header.userlog_name_offset = 0;
			}
			header.message_data_offset = header.username_data_offset + sizeof(SizeType) + username_data_size;
			header.username_set_offset = header.message_data_offset + header.message_data_size;
			header.timepoint_list_offset = header.username_set_offset + (header.username_set_size * username_set_element_size);
			header.username_list_offset = header.timepoint_list_offset + (header.number_of_lines * timepoint_list_element_size);
			header.message_list_offset = header.username_list_offset + (header.number_of_lines * username_list_element_size);
			
			header.time_period = log.getPeriod();

			//insert header
			write_type_func(header.format_mode);
			write_type_func(header.endianness);
			write_padding_func(6);
			write_type_func(header.channel_name_offset);
			write_type_func(header.userlog_name_offset);
			write_type_func(header.username_data_offset);
			write_type_func(header.message_data_offset);
			write_type_func(header.username_set_offset);
			write_type_func(header.timepoint_list_offset);
			write_type_func(header.username_list_offset);
			write_type_func(header.message_list_offset);
			write_type_func(header.number_of_lines);
			write_type_func(header.time_period);

			write_view_func(log.getChannelName());

			for (auto & name : log.getNames()) {
				write_view_func(name);
			}

			for (auto & line : log.getLines()) {
				write_view_func(line.getMessageView());
			}

			for (auto & pair : username_set) {
				write_type_func(pair.first);
				write_type_func(pair.second);
			}

			for (auto & line : log.getLines()) {
				write_type_func(TimePointType(line.getTime()));
			}

			for (DataOffsetType e : username_list) {
				write_type_func(e);
			}

			for (auto & pair : message_list) {
				write_type_func(pair.first);
				write_type_func(pair.second);
			}
		}

		template <typename ByteFieldType, typename DataOffsetType, typename SizeType, class InStream, typename FileOffsetType = std::uint64_t, typename TimePointType = TimeDetail::TimePoint, typename TimePeriodType = TimeDetail::TimePeriod>
		std::optional<Log> deserialize(InStream & in_stream)
		{
			using HeaderType = LogFileHeader<ByteFieldType, FileOffsetType, SizeType, TimePeriodType>;
			
			//read whole file
			//std::size_t in_data_position = 0;
			//bool in_data_eof = false;
			//std::vector<char> in_data((std::istreambuf_iterator<char>(in_stream)), std::istreambuf_iterator<char>());

			auto read_type_func = [&](auto & target) { in_stream.read(reinterpret_cast<char*>(&target), sizeof(target)); };
			//auto read_type_func = [&](auto & target) { if (in_data_position + sizeof(target)) >= in_data.size()) in_data_eof = true;   }
			//auto read_type_func = [&](auto & target) { std::memcpy(&target, in_data.data() + in_data_position, sizeof(target)); in_data_position += sizeof(target); };
			auto read_func = [&](void* dest, std::size_t size) { in_stream.read(reinterpret_cast<char*>(dest), size); };
			//auto read_func = [&](void* dest, std::size_t size) { std::memcpy(dest, in_data.data() + in_data_position, size); in_data_position += size; };
			//auto read_n_bytes_func = [&](std::size_t amount) { for (std::size_t i = 0; i < amount; ++i) in_stream.get(); };
			auto move_position_relative_func = [&](std::size_t size) { in_stream.seekg(std::size_t(in_stream.tellg()) + size); };
			//auto move_position_relative_func = [&](std::size_t size) {in_data_position += size; };
			auto set_position_global_func = [&](std::size_t position) { in_stream.seekg(position); };
			//auto set_position_global_func = [&](std::size_t position) {in_data_position = position; };

			HeaderType header;
			read_type_func(header.format_mode);
			read_type_func(header.endianness);
			move_position_relative_func(6);
			read_type_func(header.channel_name_offset);
			read_type_func(header.username_data_offset);
			read_type_func(header.message_data_offset);
			read_type_func(header.username_set_offset);
			read_type_func(header.timepoint_list_offset);
			read_type_func(header.username_list_offset);
			read_type_func(header.message_list_offset);
			read_type_func(header.channel_name_size);
			read_type_func(header.username_data_size);
			read_type_func(header.message_data_size);
			read_type_func(header.username_set_size);
			read_type_func(header.number_of_lines);
			read_type_func(header.time_period);

			if (in_stream.fail()) return std::nullopt;

			ChannelName channel_name;
			channel_name.resize(header.channel_name_size);
			set_position_global_func(header.channel_name_offset);
			if (in_stream.fail()) return std::nullopt;
			read_func(channel_name.data(), header.channel_name_size);

			std::vector<char> data;
			data.resize(header.username_data_size + header.message_data_size); //must not be resized after this
			char* username_data_ptr = data.data();
			char* message_data_ptr = data.data() + header.username_data_size;
			set_position_global_func(header.username_data_offset);
			if (in_stream.fail()) return std::nullopt;
			read_func(username_data_ptr, header.username_data_size);
			set_position_global_func(header.message_data_offset);
			if (in_stream.fail()) return std::nullopt;
			read_func(message_data_ptr, header.message_data_size);

			std::vector<std::string_view> username_set;
			username_set.reserve(header.username_set_size);
			set_position_global_func(header.username_set_offset);
			if (in_stream.fail()) return std::nullopt;
			for (std::size_t i = 0; i < header.username_set_size; ++i) {
				DataOffsetType offset;
				SizeType size;
				read_type_func(offset);
				read_type_func(size);
				username_set.emplace_back(username_data_ptr + offset, size);
			}

			std::vector<TimePointType> timepoint_list;
			timepoint_list.reserve(header.number_of_lines);
			set_position_global_func(header.timepoint_list_offset);
			if (in_stream.fail()) return std::nullopt;
			for (std::size_t i = 0; i < header.number_of_lines; ++i) {
				TimePointType t;
				read_type_func(t);
				timepoint_list.push_back(t);
			}

			std::vector<std::string_view> username_list;
			username_list.reserve(header.number_of_lines);
			set_position_global_func(header.username_list_offset);
			if (in_stream.fail()) return std::nullopt;
			for (std::size_t i = 0; i < header.number_of_lines; ++i) {
				DataOffsetType offset = 0;
				read_type_func(offset);
				if (offset >= username_set.size()) return std::nullopt;
				username_list.emplace_back(username_set[offset]);
			}

			std::vector<std::string_view> message_list;
			message_list.reserve(header.number_of_lines);
			set_position_global_func(header.message_list_offset);
			if (in_stream.fail()) return std::nullopt;
			for (std::size_t i = 0; i < header.number_of_lines; ++i) {
				DataOffsetType offset = 0;
				SizeType size = 0;
				read_type_func(offset);
				read_type_func(size);
				message_list.emplace_back(message_data_ptr + offset, size);
			}

			std::vector<LineView> lines;
			lines.reserve(header.number_of_lines);
			for (std::size_t i = 0; i < header.number_of_lines; ++i) {
				lines.emplace_back(timepoint_list[i], username_list[i], message_list[i]);
			}

			Log log(std::move(header.time_period), std::move(channel_name), std::move(data), std::move(username_set), std::move(lines));
			
			return std::move(log);
		}
	}

	template <class OutStream>
	void serializeDefaultLogFormat(OutStream & out_stream, const Log & log)
	{
		//using serialize_16bit = Impl::serialize<std::uint8_t, std::uint16_t, std::uint16_t, OutStream>;
		//using serialize_32bit = Impl::serialize<std::uint8_t, std::uint32_t, std::uint32_t, OutStream>;
		//using serialize_64bit = Impl::serialize<std::uint8_t, std::uint64_t, std::uint64_t, OutStream>;
		Impl::serialize<std::uint8_t, std::uint32_t, std::uint32_t, OutStream>(out_stream, log, 2, Impl::isBigEndian()?1:2);
	}

	template <class InStream>
	std::optional<Log> deserializeDefaultLogFormat(InStream & in_stream)
	{
		return std::move(Impl::deserialize<std::uint8_t, std::uint32_t, std::uint32_t, InStream>(in_stream));
	}

}

#endif // !Log_HEADER

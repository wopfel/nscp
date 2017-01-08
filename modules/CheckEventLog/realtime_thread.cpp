/*
 * Copyright 2004-2016 The NSClient++ Authors - https://nsclient.org
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <boost/foreach.hpp>

#include <format.hpp>

#include "realtime_thread.hpp"
#include "realtime_data.hpp"

#include <nscapi/nscapi_protobuf_functions.hpp>
#include <nscapi/nscapi_core_helper.hpp>
#include <nscapi/nscapi_plugin_wrapper.hpp>
#include <nscapi/nscapi_helper_singleton.hpp>
#include <nscapi/macros.hpp>

#include <parsers/filter/realtime_helper.hpp>

typedef parsers::where::realtime_filter_helper<runtime_data, eventlog_filter::filter_config_object> filter_helper;

void real_time_thread::set_path(const std::string &p) {
	filters_.set_path(p);
}

void real_time_thread::thread_proc() {
	filter_helper helper(core, plugin_id);
	std::list<std::string> logs;

	BOOST_FOREACH(const std::string &s, strEx::s::splitEx(logs_, std::string(","))) {
		logs.push_back(s);
	}

	BOOST_FOREACH(boost::shared_ptr<eventlog_filter::filter_config_object> object, filters_.get_object_list()) {
		runtime_data data;
		BOOST_FOREACH(const std::string &f, object->files) {
			if (f != "any" && f != "all") {
				logs.push_back(f);
				data.add_file(f);
			}
		}
		helper.add_item(object, data);
	}
	logs.sort();
	logs.unique();
	NSC_DEBUG_MSG_STD("Scanning logs: " + utf8::cvt<std::string>(format::join(logs, ", ")));

	typedef boost::shared_ptr<eventlog_wrapper> eventlog_type;
	typedef std::vector<eventlog_type> eventlog_list;
	eventlog_list evlog_list;

	BOOST_FOREACH(const std::string &l, logs) {
		try {
			if (eventlog::api::supports_modern()) {
				evlog_list.push_back(eventlog_type(new eventlog_wrapper_new(l)));
			} else {
				evlog_list.push_back(eventlog_type(new eventlog_wrapper_old(l)));
			}
		} catch (const error::nscp_exception &e) {
			NSC_LOG_ERROR("Failed to read eventlog " + l + ": " + e.reason());
		}
	}

	// TODO: add support for scanning "missed messages" at startup

	HANDLE *handles = new HANDLE[1 + evlog_list.size()];
	handles[0] = stop_event_;
	for (int i = 0; i < evlog_list.size(); i++) {
		evlog_list[i]->notify(handles[i + 1]);
	}
	__time64_t ltime;

	helper.touch_all();

	unsigned int errors = 0;
	while (true) {
		bool has_errors = false;
		filter_helper::op_duration dur = helper.find_minimum_timeout();

		_time64(&ltime);

		DWORD dwWaitTime = INFINITE;
		if (dur && dur->total_milliseconds() < 0)
			dwWaitTime = 0;
		else if (dur)
			dwWaitTime = dur->total_milliseconds();

		NSC_DEBUG_MSG("Sleeping for: " + strEx::s::xtos(dwWaitTime) + "ms");
		DWORD dwWaitReason = WaitForMultipleObjects(static_cast<DWORD>(evlog_list.size() + 1), handles, FALSE, dwWaitTime);
		if (dwWaitReason == WAIT_TIMEOUT) {
			helper.process_no_items();
		} else if (dwWaitReason == WAIT_OBJECT_0) {
			delete[] handles;
			return;
		} else if (dwWaitReason > WAIT_OBJECT_0 && dwWaitReason <= (WAIT_OBJECT_0 + evlog_list.size())) {
			int index = dwWaitReason - WAIT_OBJECT_0 - 1;
			try {
				eventlog_type el = evlog_list[index];
				NSC_DEBUG_MSG_STD("Detected action on: " + el->get_name());

				for (boost::shared_ptr<eventlog_filter::filter_obj> item = el->read_record(handles[index + 1]);
					item; item = el->read_record(handles[index + 1])) {
					helper.process_items(item);
				}
				el->reset_event(handles[index + 1]);
			} catch (const error::nscp_exception &e) {
				NSC_LOG_ERROR("Failed to process eventlog: " + e.reason());
				has_errors = true;
			} catch (...) {
				NSC_LOG_ERROR("Failed to process eventlog: UNKNOWN EXCEPTION");
				has_errors = true;
			}
		} else {
			NSC_LOG_ERROR("Error failed to wait for eventlog message: " + error::lookup::last_error());
			has_errors = true;
		}
		if (has_errors) {
			if (errors++ > 100) {
				NSC_LOG_ERROR("To many errors in eventlog loop giving up");
				break;
			}
		}
	}
	delete[] handles;
	return;
}

bool real_time_thread::start() {
	if (!enabled_)
		return true;
	stop_event_ = CreateEvent(NULL, TRUE, FALSE, L"EventLogShutdown");
	thread_ = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&real_time_thread::thread_proc, this)));
	return true;
}
bool real_time_thread::stop() {
	SetEvent(stop_event_);
	if (thread_)
		thread_->join();
	return true;
}

void real_time_thread::add_realtime_filter(boost::shared_ptr<nscapi::settings_proxy> proxy, std::string key, std::string query) {
	try {
		filters_.add(proxy, key, query);
	} catch (const std::exception &e) {
		NSC_LOG_ERROR_EXR("Failed to add command: " + utf8::cvt<std::string>(key), e);
	} catch (...) {
		NSC_LOG_ERROR_EX("Failed to add command: " + utf8::cvt<std::string>(key));
	}
}
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

#include "file_finder.hpp"
#include <nscapi/macros.hpp>
#include <nscapi/nscapi_helper_singleton.hpp>

#include "filter.hpp"

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif
#ifndef FILE_ATTRIBUTE_DIRECTORY
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#endif
bool file_finder::is_directory(unsigned long dwAttr) {
	if (dwAttr == INVALID_FILE_ATTRIBUTES) {
		return false;
	} else if ((dwAttr&FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY) {
		return true;
	}
	return false;
}

void file_finder::recursive_scan(file_filter::filter &filter, scanner_context &context, boost::filesystem::path dir, boost::shared_ptr<file_filter::filter_obj> total_obj, bool total_all, bool recursive, int current_level) {
	if (!context.is_valid_level(current_level)) {
		if (context.debug) context.report_debug("Level death exhausted: " + strEx::s::xtos(current_level));
		return;
	}
	WIN32_FIND_DATA wfd;

	DWORD fileAttr = GetFileAttributes(dir.wstring().c_str());
	if ((fileAttr == INVALID_FILE_ATTRIBUTES) && (!recursive)) {
		context.report_error("Invalid file specified: " + dir.string());
	} else if (fileAttr == INVALID_FILE_ATTRIBUTES) {
		context.report_warning("Invalid file specified: " + dir.string());
	}
	//if (context.debug) context.report_debug("Input is: " + dir.string() + " / " + strEx::s::xtos(fileAttr));

	if (!is_directory(fileAttr)) {
		if (context.debug) context.report_debug("Found a file won't do recursive scan: " + dir.string());
		// It is a file check it an return (don't check recursively)
		file_helpers::patterns::pattern_type single_path = file_helpers::patterns::split_path_ex(dir.string());
		if (context.debug) context.report_debug("Path is: " + single_path.first.string());
		HANDLE hFind = FindFirstFile(dir.wstring().c_str(), &wfd);
		if (hFind != INVALID_HANDLE_VALUE) {
			boost::shared_ptr<file_filter::filter_obj> info = file_filter::filter_obj::get(context.now, wfd, single_path.first);
			// boost::make_shared<eventlog_filter::filter_obj>(record, filter.summary.count_match)
			modern_filter::match_result ret = filter.match(info);
			if (total_obj && (ret.matched_filter || total_all))
				total_obj->add(info);
			FindClose(hFind);
		} else {
			context.report_error("File was NOT found!");
		}
		return;
	}
	std::string file_pattern = dir.string() + "\\" + context.pattern;
	//if (context.debug) context.report_debug("File pattern: " + file_pattern);
	HANDLE hFind = FindFirstFile(utf8::cvt<std::wstring>(file_pattern).c_str(), &wfd);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if (is_directory(wfd.dwFileAttributes) && (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0))
				continue;
			boost::shared_ptr<file_filter::filter_obj> info = file_filter::filter_obj::get(context.now, wfd, dir);
			modern_filter::match_result ret = filter.match(info);
			if (total_obj && (ret.matched_filter || total_all))
				total_obj->add(info);
		} while (FindNextFile(hFind, &wfd));
		FindClose(hFind);
	}
	std::string dir_pattern = dir.string() + "\\*.*";
	//if (context.debug) context.report_debug("File pattern: " + dir_pattern);
	hFind = FindFirstFile(utf8::cvt<std::wstring>(dir_pattern).c_str(), &wfd);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if (is_directory(wfd.dwFileAttributes)) {
				if ((wcscmp(wfd.cFileName, L".") != 0) && (wcscmp(wfd.cFileName, L"..") != 0))
					recursive_scan(filter, context, dir / wfd.cFileName, total_obj, total_all, true, current_level + 1);
			}
		} while (FindNextFile(hFind, &wfd));
		FindClose(hFind);
	}
}

bool file_finder::scanner_context::is_valid_level(int current_level) {
	return max_depth == -1 || current_level < max_depth;
}

void file_finder::scanner_context::report_error(const std::string str) {
	NSC_LOG_ERROR(str);
}

void file_finder::scanner_context::report_debug(const std::string str) {
	if (debug)
		NSC_DEBUG_MSG(str);
}

void file_finder::scanner_context::report_warning(const std::string msg) {
	NSC_LOG_ERROR(msg);
}
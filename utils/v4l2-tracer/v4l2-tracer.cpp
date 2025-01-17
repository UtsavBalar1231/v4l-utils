/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022 Collabora Ltd.
 */

#include "retrace.h"
#include <climits>
#include <sys/wait.h>
#include <time.h>

int tracer(int argc, char *argv[], bool retrace = false);

enum Options {
	V4l2TracerOptCompactPrint = 'c',
	V4l2TracerOptSetVideoDevice = 'd',
	V4l2TracerOptDebug = 'g',
	V4l2TracerOptHelp = 'h',
	V4l2TracerOptSetMediaDevice = 'm',
	V4l2TracerOptWriteDecodedToJson = 'r',
	V4l2TracerOptVerbose = 'v',
	V4l2TracerOptWriteDecodedToYUVFile = 'y',
};

const static struct option long_options[] = {
	{ "compact", no_argument, nullptr, V4l2TracerOptCompactPrint },
	{ "video_device", required_argument, nullptr, V4l2TracerOptSetVideoDevice },
	{ "debug", no_argument, nullptr, V4l2TracerOptDebug },
	{ "help", no_argument, nullptr, V4l2TracerOptHelp },
	{ "media_device", required_argument, nullptr, V4l2TracerOptSetMediaDevice },
	{ "raw", no_argument, nullptr, V4l2TracerOptWriteDecodedToJson },
	{ "verbose", no_argument, nullptr, V4l2TracerOptVerbose },
	{ "yuv", no_argument, nullptr, V4l2TracerOptWriteDecodedToYUVFile },
	{ nullptr, 0, nullptr, 0 }
};

const char short_options[] = {
	V4l2TracerOptCompactPrint,
	V4l2TracerOptSetVideoDevice, ':',
	V4l2TracerOptDebug,
	V4l2TracerOptHelp,
	V4l2TracerOptSetMediaDevice, ':',
	V4l2TracerOptWriteDecodedToJson,
	V4l2TracerOptVerbose,
	V4l2TracerOptWriteDecodedToYUVFile
};

int get_options(int argc, char *argv[])
{
	int option = 0;

	do {
		/* If there are no commands after the valid options, return err. */
		if (optind == argc) {
			print_usage();
			return -1;
		}

		/* Avoid reading the tracee's options. */
		if ((strcmp(argv[optind], "trace") == 0) || (strcmp(argv[optind], "retrace") == 0))
			return 0;

		option = getopt_long(argc, argv, short_options, long_options, NULL);
		switch (option) {
		case V4l2TracerOptCompactPrint: {
			setenv("V4L2_TRACER_OPTION_COMPACT_PRINT", "true", 0);
			break;
		}
		case V4l2TracerOptSetVideoDevice: {
			std::string device_num = optarg;
			try {
				std::stoi(device_num, nullptr, 0);
			} catch (std::exception& e) {
				fprintf(stderr, "%s:%s:%d: ", __FILE__, __func__, __LINE__);
				fprintf(stderr, "can't convert <dev> \'%s\' to integer\n", device_num.c_str());
				return -1;
			}
			if (device_num[0] >= '0' && device_num[0] <= '9' && device_num.length() <= 3) {
				std::string path_video = "/dev/video";
				path_video += optarg;
				setenv("V4L2_TRACER_OPTION_SET_VIDEO_DEVICE", path_video.c_str(), 0);
			} else {
				fprintf(stderr, "%s:%s:%d: ", __FILE__, __func__, __LINE__);
				fprintf(stderr, "cannot use device number\'%s\'\n", device_num.c_str());
				return -1;
			}
			break;
		}
		case V4l2TracerOptDebug:
			setenv("V4L2_TRACER_OPTION_VERBOSE", "true", 0);
			setenv("V4L2_TRACER_OPTION_DEBUG", "true", 0);
			break;
		case V4l2TracerOptHelp:
			print_usage();
			return -1;
		case V4l2TracerOptSetMediaDevice: {
			std::string device_num = optarg;
			try {
				std::stoi(device_num, nullptr, 0);
			} catch (std::exception& e) {
				fprintf(stderr, "%s:%s:%d: ", __FILE__, __func__, __LINE__);
				fprintf(stderr, "can't convert <dev> \'%s\' to integer\n", device_num.c_str());
				return -1;
			}
			if (device_num[0] >= '0' && device_num[0] <= '9' && device_num.length() <= 3) {
				std::string path_media = "/dev/media";
				path_media += optarg;
				setenv("V4L2_TRACER_OPTION_SET_MEDIA_DEVICE", path_media.c_str(), 0);
			} else {
				fprintf(stderr, "%s:%s:%d: ", __FILE__, __func__, __LINE__);
				fprintf(stderr, "cannot use device number\'%s\'\n", device_num.c_str());
				return -1;
			}
			break;
		}
		case V4l2TracerOptWriteDecodedToJson:
			setenv("V4L2_TRACER_OPTION_WRITE_DECODED_TO_JSON_FILE", "true", 0);
			break;
		case V4l2TracerOptVerbose:
			setenv("V4L2_TRACER_OPTION_VERBOSE", "true", 0);
			break;
		case V4l2TracerOptWriteDecodedToYUVFile:
			setenv("V4L2_TRACER_OPTION_WRITE_DECODED_TO_YUV_FILE", "true", 0);
			break;
		default:
			break;
		}

		/* invalid option */
		if (optopt > 0) {
			print_usage();
			return -1;
		}

	} while (option != -1);

	return 0;
}

int clean(std::string trace_filename)
{
	FILE *trace_file = fopen(trace_filename.c_str(), "r");
	if (trace_file == nullptr) {
		fprintf(stderr, "%s:%s:%d: ", __FILE__, __func__, __LINE__);
		fprintf(stderr, "cannot open \'%s\'\n", trace_filename.c_str());
		return 1;
	}

	fprintf(stderr, "Cleaning: %s\n", trace_filename.c_str());

	std::string clean_filename = "clean_" + trace_filename;
	FILE *clean_file = fopen(clean_filename.c_str(), "w");
	if (clean_file == nullptr) {
		fprintf(stderr, "%s:%s:%d: ", __FILE__, __func__, __LINE__);
		fprintf(stderr, "cannot open \'%s\'\n", clean_filename.c_str());
		return 1;
	}

	std::string line;
	char buf[SHRT_MAX];
	int count_total = 0;
	int count_lines_removed = 0;

	while (fgets(buf, SHRT_MAX, trace_file) != nullptr) {
		line = buf;
		count_total++;
		if (line.find("fd") != std::string::npos) {
			count_lines_removed++;
			continue;
		}
		if (line.find("address") != std::string::npos) {
			count_lines_removed++;
			continue;
		}
		if (line.find("fildes") != std::string::npos) {
			count_lines_removed++;
			continue;
		}
		if (line.find("\"start\"") != std::string::npos) {
			count_lines_removed++;
			continue;
		}
		if (line.find("\"name\"") != std::string::npos) {
			count_lines_removed++;
			continue;
		}

		fputs(buf, clean_file);
	}

	fclose(trace_file);
	fclose(clean_file);
	fprintf(stderr, "Removed %d lines of %d total lines: %s\n",
	        count_lines_removed, count_total, clean_filename.c_str());

	return 0;
}

int tracer(int argc, char *argv[], bool retrace)
{
	char *exec[argc];
	int exec_index = 0;

	char retrace_command[] = "__retrace";

	if (retrace) {
		std::string trace_file = argv[optind];
		if (trace_file.find(".json") == std::string::npos) {
			fprintf(stderr, "%s:%s:%d: ", __FILE__, __func__, __LINE__);
			fprintf(stderr, "Trace file \'%s\' must have .json file extension\n",
			        trace_file.c_str());
			print_usage();
			return -1;
		}
	}

	/* Get the application to be traced. */
	if (retrace) {
		exec[exec_index++] = argv[0]; /* tracee is v4l2-tracer, local or installed */
		exec[exec_index++] = retrace_command;
		exec[exec_index++] = argv[optind]; /* json file to be retraced */
	} else {
		while (optind < argc)
			exec[exec_index++] = argv[optind++];
	}
	exec[exec_index] = nullptr;

	/* Create a unique trace filename and open a file. */
	std::string trace_id;
	if (retrace) {
		std::string json_file_name = argv[optind];
		trace_id = json_file_name.substr(0, json_file_name.find(".json"));
		trace_id += "_retrace";
	} else {
		const int timestamp_start_pos = 5;
		trace_id = std::to_string(time(nullptr));
		// trace_id = trace_id.substr(timestamp_start_pos, std::string::npos) + "_trace";
		trace_id = trace_id.substr(timestamp_start_pos) + "_trace";

	}
	setenv("TRACE_ID", trace_id.c_str(), 0);
	std::string trace_filename = trace_id + ".json";
	FILE *trace_file = fopen(trace_filename.c_str(), "w");
	if (trace_file == nullptr) {
		fprintf(stderr, "Could not open trace file: %s\n", trace_filename.c_str());
		perror("");
		return errno;
	}

	/* Open the json array.*/
	fputs("[\n", trace_file);

	/* Add v4l-utils package and git info to the top of the trace file. */
	std::string json_str;
	json_object *v4l2_tracer_info_obj = json_object_new_object();
	json_object_object_add(v4l2_tracer_info_obj, "package_version",
	                       json_object_new_string(PACKAGE_VERSION));
	std::string git_commit_cnt = STRING(GIT_COMMIT_CNT);
	git_commit_cnt = git_commit_cnt.erase(0, 1); /* remove the hyphen in front of git_commit_cnt */
	json_object_object_add(v4l2_tracer_info_obj, "git_commit_cnt",
	                       json_object_new_string(git_commit_cnt.c_str()));
	json_object_object_add(v4l2_tracer_info_obj, "git_sha",
	                       json_object_new_string(STRING(GIT_SHA)));
	json_object_object_add(v4l2_tracer_info_obj, "git_commit_date",
	                       json_object_new_string(STRING(GIT_COMMIT_DATE)));
	json_str = json_object_to_json_string(v4l2_tracer_info_obj);
	fwrite(json_str.c_str(), sizeof(char), json_str.length(), trace_file);
	fputs(",\n", trace_file);
	json_object_put(v4l2_tracer_info_obj);

	/* Add v4l2-tracer command line to the top of the trace file. */
	json_object *tracee_obj = json_object_new_object();
	std::string tracee;
	for (int i = 0; i < argc; i++) {
		tracee += argv[i];
		tracee += " ";
	}
	json_object_object_add(tracee_obj, "Trace", json_object_new_string(tracee.c_str()));
	const time_t current_time = time(nullptr);
	json_object_object_add(tracee_obj, "Timestamp", json_object_new_string(ctime(&current_time)));

	json_str = json_object_to_json_string(tracee_obj);
	fwrite(json_str.c_str(), sizeof(char), json_str.length(), trace_file);
	fputs(",\n", trace_file);
	json_object_put(tracee_obj);
	fclose(trace_file);

	/*
	 * Preload the libv4l2tracer library. If the program is installed, load the library
	 * from its installed location, otherwise load it locally. If it's loaded locally,
	 * use ./configure --disable-dyn-libv4l.
	 */
	std::string libv4l2tracer_path;
	std::string program = argv[0];
	std::size_t idx = program.rfind("/v4l2-tracer");
	if (idx != std::string::npos) {
		libv4l2tracer_path = program.replace(program.begin() + idx + 1, program.end(), ".libs");
		DIR *directory_pointer = opendir(libv4l2tracer_path.c_str());
		if (directory_pointer == nullptr)
			libv4l2tracer_path = program.replace(program.begin() + idx, program.end(), "./.libs");
		else
			closedir(directory_pointer);
	} else {
		libv4l2tracer_path = STRING(LIBTRACER_PATH);
	}
	libv4l2tracer_path += "/libv4l2tracer.so";
	if (is_verbose())
		fprintf(stderr, "Loading libv4l2tracer: %s\n", libv4l2tracer_path.c_str());
	setenv("LD_PRELOAD", libv4l2tracer_path.c_str(), 0);

	if (fork() == 0) {

		if (is_debug()) {
			fprintf(stderr, "%s:%s:%d: ", __FILE__, __func__, __LINE__);
			fprintf(stderr, "tracee: ");
			for (int i = 0; i < exec_index; i++)
				fprintf(stderr,"%s ", exec[i]);
			fprintf(stderr, "\n");
		}

		execvpe(exec[0], (char* const*) exec, environ);

		fprintf(stderr, "%s:%s:%d: ", __FILE__, __func__, __LINE__);
		fprintf(stderr, "could not execute application \'%s\'", exec[0]);
		perror(" ");
		return errno;
	}

	int exec_result = 0;
	wait(&exec_result);

	if (WEXITSTATUS(exec_result)) {
		fprintf(stderr, "Trace error: %s\n", trace_filename.c_str());

		trace_file = fopen(trace_filename.c_str(), "a");
		fseek(trace_file, 0L, SEEK_END);
		fputs("\n]\n", trace_file);
		fclose(trace_file);

		exit(EXIT_FAILURE);
	}

	/* Close the json-array and the trace file. */
	trace_file = fopen(trace_filename.c_str(), "a");
	fseek(trace_file, 0L, SEEK_END);
	fputs("\n]\n", trace_file);
	fclose(trace_file);

	if (retrace)
		fprintf(stderr, "Retrace complete: ");
	else
		fprintf(stderr, "Trace complete: ");
	fprintf(stderr, "%s", trace_filename.c_str());
	fprintf(stderr, "\n");

	return 0;
}

int main(int argc, char *argv[])
{
	int ret = -1;

	if (argc <= 1) {
		print_usage();
		return ret;
	}

	ret = get_options(argc, argv);

	if (ret < 0) {
		if (is_debug())
			fprintf(stderr, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
		return ret;
	}

	if (optind == argc) {
		if (is_debug())
			fprintf(stderr, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
		print_usage();
		return ret;
	}

	std::string command = argv[optind++];

	if (optind == argc) {
		if (is_debug())
			fprintf(stderr, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
		print_usage();
		return ret;
	}

	if (command == "trace") {
		ret = tracer(argc, argv);
	} else if (command == "retrace") {
		ret = tracer(argc, argv, true);
	} else if (command == "__retrace") {
		/*
		 * This command is meant to be used only internally to allow
		 * v4l2-tracer to recursively trace itself during a retrace.
		 */
		ret = retrace(argv[optind]);
	} else if (command == "clean") {
		ret = clean (argv[optind]);
	} else {
		if (is_debug()) {
			fprintf(stderr, "%s:%s:%d\n", __FILE__, __func__, __LINE__);
			fprintf(stderr, "tracee: ");
			for (int i = 0; i < argc; i++)
				fprintf(stderr,"%s ", argv[i]);
			fprintf(stderr, "\n");
		}
		print_usage();
	}

	return ret;
}

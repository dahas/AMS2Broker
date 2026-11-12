#pragma once
#include "pch.h"
#include <string>
#include <vector>
#include <map>

std::string StripComments(const std::string& s);

std::string GetValueAtPath(const std::string& json,
    const std::vector<std::string>& path);

bool GetIntAtPath(const std::string& json,
    const std::vector<std::string>& path,
    long long& out);

bool ParseClassStates(const std::string& json, std::map<int, int>& out);
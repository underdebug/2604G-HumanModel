#pragma once
#include "makehuman.h"
#include <vector>

// Fitted short-sleeved T-shirt derived from a human mesh in metres.
std::vector<makehuman::MeshVertex> buildShirt(const std::vector<makehuman::MeshVertex> &body);

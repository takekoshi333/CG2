#pragma once
#include "Vector4.h"
#include "Matrix3x3.h"

struct Material {
	Vector4 color;
	int32_t enableLighting;
	Matrix3x3 uvTransform;
};
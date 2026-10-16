// DynWorld
// Copyright (C) 2014 Kishan Grimout <kishan.grimout@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

/* Original version from:
Minetest
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
Copyright (C) 2010-2013 kwolekr, Ryan Kwolek <kwolekr@minetest.net>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef NOISE_HEADER
#define NOISE_HEADER

#include "nel/misc/debug.h"
#include "nel/misc/vector.h"
#include "nel/misc/config_file.h"

class PseudoRandom
{
public:
	PseudoRandom(): m_next(0)
	{
	}
	PseudoRandom(int seed): m_next(seed)
	{
	}
	void seed(int seed)
	{
		m_next = seed;
	}
	// Returns 0...32767
	int next()
	{
		m_next = m_next * 1103515245 + 12345;
		return((unsigned)(m_next/65536) % 32768);
	}
	int range(int min, int max)
	{
		if(max-min > 32768/10)
		{
			//dstream<<"WARNING: PseudoRandom::range: max > 32767"<<std::endl;
			nlassert(0);
		}
		if(min > max)
		{
			nlassert(0);
			return max;
		}
		return (next()%(max-min+1))+min;
	}
private:
	int m_next;
};

struct NoiseParams {
	float offset;
	float scale;
	NLMISC::CVector spread;
	int seed;
	int octaves;
	float persist;

	NoiseParams() {}

	NoiseParams(float offset_, float scale_, NLMISC::CVector spread_,
		int seed_, int octaves_, float persist_)
	{
		offset  = offset_;
		scale   = scale_;
		spread  = spread_;
		seed    = seed_;
		octaves = octaves_;
		persist = persist_;
	}

	void load(NLMISC::CConfigFile::CVar& var);
};

class NoiseSample {
public:
	NLMISC::CVector derivative;
	float value;

	NoiseSample& operator += (const NoiseSample& other)
	{
		value += other.value;
		derivative += other.derivative;
		return *this;
	}

	NoiseSample& operator += (float k)
	{
		value += k;
		return *this;
	}

	NoiseSample& operator -= (const NoiseSample& other)
	{
		value -= other.value;
		derivative -= other.derivative;
		return *this;
	}

	NoiseSample& operator -= (float k)
	{
     value -= k;
		return *this;
	}

	NoiseSample& operator *= (const NoiseSample& other)
	{
       const float oldValue = value;
		derivative = derivative * other.value + oldValue * other.derivative;
		value = oldValue * other.value;
		return *this;
	}

	NoiseSample& operator *= (float k)
	{
		value *= k;
		derivative *= k;
		return *this;
	}
};

class Noise {
public:
	NoiseParams *np;
	int seed;
	int sx;
	int sy;
	int sz;
	float *noisebuf;
	float *buf;
	float *result;

	Noise(NoiseParams *np, int seed, int sx, int sy, int sz = 1);
	virtual ~Noise();

	virtual void init(NoiseParams *np, int seed, int sx, int sy, int sz = 1);
	void setSize(int sx, int sy);
	void setSize(int sx, int sy, int sz);
	void setSpreadFactor(NLMISC::CVector spread);
	void setOctaves(int octaves);
	void resizeNoiseBuf(bool is3d);

	void gradientMap2D(
		float x, float y,
		float step_x, float step_y,
		int seed);
	void gradientMap3D(
		float x, float y, float z,
		float step_x, float step_y, float step_z,
		int seed);
	float *perlinMap2D(float x, float y);
	float *perlinMap2DModulated(float x, float y, float *persist_map);
	float *perlinMap3D(float x, float y, float z);
	void transformNoiseMap();
};

// Return value: -1 ... 1
float noise2d(int x, int y, int seed);
float noise3d(int x, int y, int z, int seed);

NoiseSample noise2d_derivatives(int x, int y, int seed);

float noise2d_gradient(float x, float y, int seed);
float noise3d_gradient(float x, float y, float z, int seed);

float noise2d_perlin(float x, float y, int seed,
		int octaves, float persistence);

NoiseSample noise2d_perlin_derivatives(float x, float y, int seed,
	int octaves, float persistence);

float noise2d_perlin_abs(float x, float y, int seed,
		int octaves, float persistence);

float noise3d_perlin(float x, float y, float z, int seed,
		int octaves, float persistence);

float noise3d_perlin_abs(float x, float y, float z, int seed,
		int octaves, float persistence);

inline float easeCurve(float t) {
	return t * t * t * (t * (6.f * t - 15.f) + 10.f);
}

inline float easeCurve_derivative(float t) {
	// f(t) = (6*t*t*t*t*t - 15*t*t*t*t + 10*t*t*t)
	// f'(t) = 6*5*t*t*t*t - 15*4*t*t*t + 10*3*t*t
	//       = t*t*(30*t*t - 60*t + 30)
	//       = t*t*(t*(30*t - 60) + 30)
	return 30.f * t * t *(t * (t - 2.f) + 1.f);
}

inline float NoisePerlin2D(const NoiseParams* np, float _x, float _y, int s)
{
	return np->offset + np->scale * noise2d_perlin(
		_x / np->spread.x,
		_y / np->spread.y,
		s + np->seed, np->octaves, np->persist);
}

inline float NoisePerlin2DNoTxfm(const NoiseParams* np, float _x, float _y, int s)
{
	return noise2d_perlin(
		_x / np->spread.x,
		_y / np->spread.y,
		s + np->seed, np->octaves, np->persist);
}

inline float NoisePerlin2DPosOffset(const NoiseParams* np, float _x, float xoff, float _y, float yoff, int s)
{
	return np->offset + np->scale * noise2d_perlin(
		xoff + _x / np->spread.x,
		yoff + _y / np->spread.y,
		s + np->seed, np->octaves, np->persist);
}

inline NoiseSample NoisePerlin2DPosOffset_derivatives(const NoiseParams* np, float _x, float xoff, float _y, float yoff, int s)
{
	NoiseSample noise = noise2d_perlin_derivatives(
		xoff + _x / np->spread.x,
		yoff + _y / np->spread.y,
		s + np->seed, np->octaves, np->persist);
	noise *= np->scale;
	noise += np->offset;
	return noise;
}

inline float NoisePerlin2DNoTxfmPosOffset(const NoiseParams* np, float _x, float xoff, float _y, float yoff, int s)
{
	return noise2d_perlin(
		xoff + _x / np->spread.x,
		yoff + _y / np->spread.y,
		s + np->seed, np->octaves, np->persist);
}

inline float NoisePerlin3D(const NoiseParams* np, float _x, float _y, float _z, int s)
{
	return np->offset + np->scale *
		noise3d_perlin(_x / np->spread.x, _y / np->spread.y,
		_z / np->spread.z, s + np->seed, np->octaves, np->persist);
}

#endif


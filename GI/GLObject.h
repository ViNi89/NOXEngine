#pragma once

#include "GLUtils.h"
#include "CustomTypes.h"
#include "Constants.h"

template<class T>
struct nxGLObject {

public:

	nxGLObject(nxUInt32 id) : m_ID{ id } {}
	nxGLObject() : m_ID{ -1 } {}

	GLuint ID() { return m_ID; }
	operator unsigned int() { return m_ID; }
	operator unsigned int*() { return &m_ID; }
	operator const unsigned int*() { return &m_ID; }
	void operator=(int id) { m_ID = id; }
	friend bool operator==(nxGLObject<T> const& i_lhs, nxGLObject<T> const& i_rhs);

private:

	GLuint			m_ID;

};

struct NOXProgramObject;
typedef nxGLObject<NOXProgramObject> nxProgramObject;
struct NOXShaderObject{};
typedef nxGLObject<NOXShaderObject> nxShaderObject;
struct NOXStorageBufferObject;
typedef nxGLObject<NOXStorageBufferObject> nxStorageBufferObject;
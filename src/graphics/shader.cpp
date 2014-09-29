#include "graphics/shader.h"
#include "core/fs/file_system.h"
#include "core/fs/ifile.h"
#include "core/json_serializer.h"
#include "core/log.h"
#include "core/matrix.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec3.h"
#include "graphics/gl_ext.h"
#include "graphics/shader_manager.h"


namespace Lumix
{


Shader::Shader(const Path& path, ResourceManager& resource_manager)
	: Resource(path, resource_manager)
	, m_vertex_id()
	, m_fragment_id()
	, m_is_shadowmap_required(true)
{
	m_program_id = glCreateProgram();
}

Shader::~Shader()
{
	glDeleteProgram(m_program_id);
	glDeleteShader(m_vertex_id);
	glDeleteShader(m_fragment_id);
}


GLint Shader::getUniformLocation(const char* name, uint32_t name_hash)
{
	for (int i = 0, c = m_uniforms.size(); i < c; ++i)
	{
		if (m_uniforms[i].m_name_hash == name_hash)
		{
			return m_uniforms[i].m_location;
		}
	}
	CachedUniform& unif = m_uniforms.pushEmpty();
	unif.m_name_hash = name_hash;
	unif.m_location = glGetUniformLocation(m_program_id, name);
	return unif.m_location;
}


GLuint Shader::attach(GLenum type, const char* src, int32_t length)
{
	GLuint id = glCreateShader(type);
	glShaderSource(id, 1, (const GLchar**)&src, &length);
	glCompileShader(id);
	glAttachShader(m_program_id, id);
	return id;
}

void Shader::loaded(FS::IFile* file, bool success, FS::FileSystem& fs)
{
	if(success)
	{
		JsonSerializer serializer(*file, JsonSerializer::READ, m_path.c_str());
		serializer.deserializeObjectBegin();
		char attributes[MAX_ATTRIBUTE_COUNT][31];
		int attribute_count = 0;
		while (!serializer.isObjectEnd())
		{
			char label[256];
			serializer.deserializeLabel(label, 255);
			if (strcmp(label, "attributes") == 0)
			{
				serializer.deserializeArrayBegin();
				while (!serializer.isArrayEnd())
				{
					serializer.deserializeArrayItem(attributes[attribute_count], 30);
					++attribute_count;
					if (attribute_count == MAX_ATTRIBUTE_COUNT)
					{
						g_log_error.log("renderer") << "Too many vertex attributes in shader " << m_path.c_str();
						onFailure();
						fs.close(file);
						return;
					}
				}
				serializer.deserializeArrayEnd();
			}
			else if (strcmp(label, "shadowmap_required") == 0)
			{
				serializer.deserialize(m_is_shadowmap_required);
			}
		}
		serializer.deserializeObjectEnd();
		
		int32_t size = serializer.getRestOfFileSize();
		ShaderManager* manager = static_cast<ShaderManager*>(getResourceManager().get(ResourceManager::SHADER));
		char* buf = reinterpret_cast<char*>(manager->getBuffer(size + 1));
		serializer.deserializeRawString(buf, size);
		buf[size] = '\0';

		char* end = strstr(buf, "//~VS");		
		if (!end)
		{
			g_log_error.log("renderer") << "Could not process shader file " << m_path.c_str();
			onFailure();
			fs.close(file);
			return;
		}
		int32_t vs_len = (int32_t)(end - buf);
		buf[vs_len-1] = 0;
		m_vertex_id = attach(GL_VERTEX_SHADER, buf, vs_len);
		m_fragment_id = attach(GL_FRAGMENT_SHADER, buf + vs_len, size - vs_len);
		glLinkProgram(m_program_id);
		GLint link_status;
		glGetProgramiv(m_program_id, GL_LINK_STATUS, &link_status);
		if (link_status != GL_TRUE)
		{
			g_log_error.log("renderer") << "Could not link shader " << m_path.c_str();
			onFailure();
			fs.close(file);
			return;
		}

		for (int i = 0; i < attribute_count; ++i)
		{
			m_vertex_attributes_ids[i] = glGetAttribLocation(m_program_id, attributes[i]);
		}
		m_fixed_cached_uniforms[(int)FixedCachedUniforms::WORLD_MATRIX] = glGetUniformLocation(m_program_id, "world_matrix");
		m_fixed_cached_uniforms[(int)FixedCachedUniforms::GRASS_MATRICES] = glGetUniformLocation(m_program_id, "grass_matrices");
		m_fixed_cached_uniforms[(int)FixedCachedUniforms::MORPH_CONST] = glGetUniformLocation(m_program_id, "morph_const");
		m_fixed_cached_uniforms[(int)FixedCachedUniforms::QUAD_SIZE] = glGetUniformLocation(m_program_id, "quad_size");
		m_fixed_cached_uniforms[(int)FixedCachedUniforms::QUAD_MIN] = glGetUniformLocation(m_program_id, "quad_min");

		m_size = file->size();
		decrementDepCount();
	}
	else
	{
		g_log_error.log("renderer") << "Could not load shader " << m_path.c_str();
		onFailure();
	}

	fs.close(file);
}

void Shader::doUnload(void)
{
	m_uniforms.clear();
	glDeleteProgram(m_program_id);
	glDeleteShader(m_vertex_id);
	glDeleteShader(m_fragment_id);
	m_program_id = glCreateProgram();
	m_vertex_id = 0;
	m_fragment_id = 0;

	m_size = 0;
	onEmpty();
}

FS::ReadCallback Shader::getReadCallback()
{
	FS::ReadCallback cb;
	cb.bind<Shader, &Shader::loaded>(this);
	return cb;
}


} // ~namespace Lumix

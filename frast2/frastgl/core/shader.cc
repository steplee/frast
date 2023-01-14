#include "shader.h"

#include <cassert>
#include <fmt/core.h>

namespace frast {

	Shader::~Shader() {
		if (prog != 0) glDeleteProgram(prog);
	}

	bool Shader::compile(const std::string& vsrc, const std::string& fsrc) {

		auto vs = compileShader(vsrc, GL_VERTEX_SHADER);
		auto fs = compileShader(fsrc, GL_FRAGMENT_SHADER);

		prog = glCreateProgram();

		glAttachShader(prog, vs);
		glAttachShader(prog, fs);

		glLinkProgram(prog);


		GLint isLinked;
		glGetProgramiv(prog, GL_LINK_STATUS, (int *)&isLinked);

		if (isLinked == GL_FALSE) {
			GLint logSize = 0;
			glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logSize);

			std::string errorLog(logSize, ' ');
			glGetProgramInfoLog(prog, logSize, &logSize, &errorLog[0]);

			fmt::print("\n*****************************************************\n");
			fmt::print("\nShaders failed to Link, with message\n{}\n", errorLog);
			fmt::print("*****************************************************\n");
			assert(false);
		}

		glDeleteShader(vs);
		glDeleteShader(fs);

		return false;

	}

	GLuint Shader::compileShader(const std::string& src, int type) {


		GLuint shader = glCreateShader(type);

		const char* src_ = src.c_str();
		glShaderSource(shader, 1, &src_, nullptr);

		glCompileShader(shader);

		GLint success = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

		if (success == GL_FALSE) {
			GLint logSize = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logSize);

			std::string errorLog(logSize, ' ');
			glGetShaderInfoLog(shader, logSize, &logSize, &errorLog[0]);

			fmt::print("\n*****************************************************\n");
			fmt::print("\nShader failed to compile, with message\n{}\n", errorLog);
			fmt::print("*****************************************************\n");
			assert(false);
		}

		return shader;
	}

}

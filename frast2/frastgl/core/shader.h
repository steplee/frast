#pragma once

#include <GL/glew.h>
#include <GL/gl.h>

#include <string>

namespace frast {

	struct Shader {

		public:

			inline Shader() : prog(0) {}
			inline Shader(const std::string& vsrc, const std::string& fsrc) {
				compile(vsrc,fsrc);
			}

			~Shader();

			Shader(const Shader&) = delete;
			Shader& operator=(const Shader&) = delete;

			inline Shader(Shader&& o) {
				auto oo = o.prog;
				o.prog = prog;
				prog = oo;
			}
			inline Shader& operator=(Shader&& o) {
				auto oo = o.prog;
				o.prog = prog;
				prog = oo;
				return *this;
			}

		public:

			GLuint prog=0;

			bool compile(const std::string& vsrc, const std::string& fsrc);

		private:

			GLuint compileShader(const std::string& src, int type);

	};

}


// Should be optimized heavily with -O3.
// But could also hand-implement SSE/NEON intrinsics to ensure fast.
//
// TODO XXX: What about jpeg compression? Like zeros may be uncompressed to ones and we'd lose the nodata flag...
//           Perhaps check like [0,1,2] as nodata...
//
void Image::add_nodata__average_(const Image& other, uint16_t nodata) {
	int n = size();
	int wh = w*h;
	int c = channels();
	if (c == 1) {
		for (int i=0; i<wh; i++) {
			uint16_t a = static_cast<uint16_t>(buffer[i]);
			uint16_t b = static_cast<uint16_t>(other.buffer[i]);
			buffer[i] = static_cast<uint8_t>(
				(a == nodata) ? b : (b == nodata) ? a : (a+b)/2);
		}
	} else if (c == 3) {
		for (int i=0; i<wh; i++) {
			uint16_t a[3] = {
				static_cast<uint16_t>(buffer[i*3+0]),
				static_cast<uint16_t>(buffer[i*3+1]),
				static_cast<uint16_t>(buffer[i*3+2]) };
			uint16_t b[3] = {
				static_cast<uint16_t>(other.buffer[i*3+0]),
				static_cast<uint16_t>(other.buffer[i*3+1]),
				static_cast<uint16_t>(other.buffer[i*3+2]) };
			bool a_bad = a[0] == nodata and a[1] == nodata and a[2] == nodata;
			bool b_bad = b[0] == nodata and b[1] == nodata and b[2] == nodata;
			buffer[i*3+0] = static_cast<uint8_t>(a_bad ? b[0] : b_bad ? a[0] : (a[0]+b[0])/2);
			buffer[i*3+1] = static_cast<uint8_t>(a_bad ? b[1] : b_bad ? a[1] : (a[1]+b[1])/2);
			buffer[i*3+2] = static_cast<uint8_t>(a_bad ? b[2] : b_bad ? a[2] : (a[2]+b[2])/2);
		}
	} else if (c == 4) {
		for (int i=0; i<wh; i++) {
			uint16_t a[4] = {
				static_cast<uint16_t>(buffer[i*4+0]),
				static_cast<uint16_t>(buffer[i*4+1]),
				static_cast<uint16_t>(buffer[i*4+2]),
				static_cast<uint16_t>(buffer[i*4+3]), };
			uint16_t b[4] = {
				static_cast<uint16_t>(other.buffer[i*4+0]),
				static_cast<uint16_t>(other.buffer[i*4+1]),
				static_cast<uint16_t>(other.buffer[i*4+2]),
				static_cast<uint16_t>(other.buffer[i*4+3]) };
			bool a_bad = a[0] == nodata and a[1] == nodata and a[2] == nodata and a[3] == nodata;
			bool b_bad = b[0] == nodata and b[1] == nodata and b[2] == nodata and b[3] == nodata;
			buffer[i*4+0] = static_cast<uint8_t>(a_bad ? b[0] : b_bad ? a[0] : (a[0]+b[0])/2);
			buffer[i*4+1] = static_cast<uint8_t>(a_bad ? b[1] : b_bad ? a[1] : (a[1]+b[1])/2);
			buffer[i*4+2] = static_cast<uint8_t>(a_bad ? b[2] : b_bad ? a[2] : (a[2]+b[2])/2);
			buffer[i*4+3] = static_cast<uint8_t>(a_bad ? b[3] : b_bad ? a[3] : (a[3]+b[3])/2);
		}
	} else {
		throw std::runtime_error("not supported yet.");
	}
}
void Image::add_nodata__keep_(const Image& other, uint16_t nodata) {
	int n = size();
	int wh = w*h;
	int c = channels();
	if (c == 1) {
		for (int i=0; i<wh; i++) {
			uint16_t a = static_cast<uint16_t>(buffer[i]);
			uint16_t b = static_cast<uint16_t>(other.buffer[i]);
			buffer[i] = static_cast<uint8_t>(
				(a == nodata) ? b : (b == nodata) ? a : a);
		}
	} else if (c == 3) {
		for (int i=0; i<wh; i++) {
			uint16_t a[3] = {
				static_cast<uint16_t>(buffer[i*3+0]),
				static_cast<uint16_t>(buffer[i*3+1]),
				static_cast<uint16_t>(buffer[i*3+2]) };
			uint16_t b[3] = {
				static_cast<uint16_t>(other.buffer[i*3+0]),
				static_cast<uint16_t>(other.buffer[i*3+1]),
				static_cast<uint16_t>(other.buffer[i*3+2]) };
			bool a_bad = a[0] == nodata and a[1] == nodata and a[2] == nodata;
			bool b_bad = b[0] == nodata and b[1] == nodata and b[2] == nodata;
			buffer[i*3+0] = static_cast<uint8_t>(a_bad ? b[0] : b_bad ? a[0] : a[0]);
			buffer[i*3+1] = static_cast<uint8_t>(a_bad ? b[1] : b_bad ? a[1] : a[1]);
			buffer[i*3+2] = static_cast<uint8_t>(a_bad ? b[2] : b_bad ? a[2] : a[2]);
		}
	} else if (c == 4) {
		for (int i=0; i<wh; i++) {
			uint16_t a[4] = {
				static_cast<uint16_t>(buffer[i*4+0]),
				static_cast<uint16_t>(buffer[i*4+1]),
				static_cast<uint16_t>(buffer[i*4+2]),
				static_cast<uint16_t>(buffer[i*4+3]), };
			uint16_t b[4] = {
				static_cast<uint16_t>(other.buffer[i*4+0]),
				static_cast<uint16_t>(other.buffer[i*4+1]),
				static_cast<uint16_t>(other.buffer[i*4+2]),
				static_cast<uint16_t>(other.buffer[i*4+3]) };
			bool a_bad = a[0] == nodata and a[1] == nodata and a[2] == nodata and a[3] == nodata;
			bool b_bad = b[0] == nodata and b[1] == nodata and b[2] == nodata and b[3] == nodata;
			buffer[i*4+0] = static_cast<uint8_t>(a_bad ? b[0] : b_bad ? a[0] : a[0]);
			buffer[i*4+1] = static_cast<uint8_t>(a_bad ? b[1] : b_bad ? a[1] : a[1]);
			buffer[i*4+2] = static_cast<uint8_t>(a_bad ? b[2] : b_bad ? a[2] : a[2]);
			buffer[i*4+3] = static_cast<uint8_t>(a_bad ? b[3] : b_bad ? a[3] : a[3]);
		}
	} else {
		throw std::runtime_error("not supported yet.");
	}
}
void Image::add_nodata__weighted_(const Image& other, uint32_t nodata_) {
	int32_t nodata = nodata_;
	int n = size();
	int wh = w*h;
	int c = channels();
	if (c == 1) {
		for (int i=0; i<wh; i++) {
			uint32_t a = static_cast<uint32_t>(buffer[i]);
			uint32_t b = static_cast<uint32_t>(other.buffer[i]);
			uint32_t w1 = 255 - std::abs((int)a-nodata);
			uint32_t w2 = 255 - std::abs((int)b-nodata);
			buffer[i] = static_cast<uint8_t>((a*w1 + b*w2) / (w1+w2));
		}
	} else if (c == 3) {
		for (int i=0; i<wh; i++) {

			uint32_t a[3] = {
				static_cast<uint32_t>(buffer[i*3+0]),
				static_cast<uint32_t>(buffer[i*3+1]),
				static_cast<uint32_t>(buffer[i*3+2]) };
			uint32_t b[3] = {
				static_cast<uint32_t>(other.buffer[i*3+0]),
				static_cast<uint32_t>(other.buffer[i*3+1]),
				static_cast<uint32_t>(other.buffer[i*3+2]) };
			uint32_t w1 = 1 + (std::abs((int)a[0]-nodata) + std::abs((int)a[1]-nodata) + std::abs((int)a[2]-nodata)) / 3;
			uint32_t w2 = 1 + (std::abs((int)b[0]-nodata) + std::abs((int)b[1]-nodata) + std::abs((int)b[2]-nodata)) / 3;

			//w1 = w1 * w1;
			//w2 = w2 * w2;
			buffer[i*3+0] = static_cast<uint8_t>((a[0]*w1 + b[0]*w2) / (w1+w2));
			buffer[i*3+1] = static_cast<uint8_t>((a[1]*w1 + b[1]*w2) / (w1+w2));
			buffer[i*3+2] = static_cast<uint8_t>((a[2]*w1 + b[2]*w2) / (w1+w2));
		}
	} else if (c == 4) {
		for (int i=0; i<wh; i++) {
			uint32_t a[4] = {
				static_cast<uint32_t>(buffer[i*4+0]),
				static_cast<uint32_t>(buffer[i*4+1]),
				static_cast<uint32_t>(buffer[i*4+2]),
				static_cast<uint32_t>(buffer[i*4+3]), };
			uint32_t b[4] = {
				static_cast<uint32_t>(other.buffer[i*4+0]),
				static_cast<uint32_t>(other.buffer[i*4+1]),
				static_cast<uint32_t>(other.buffer[i*4+2]),
				static_cast<uint32_t>(other.buffer[i*4+3]) };
			uint32_t w1 = 3*255 - std::abs((int)a[0]-nodata) - std::abs((int)a[1]-nodata) - std::abs((int)a[2]-nodata) - std::abs((int)a[3]-nodata);
			uint32_t w2 = 3*255 - std::abs((int)b[0]-nodata) - std::abs((int)b[1]-nodata) - std::abs((int)b[2]-nodata) - std::abs((int)b[3]-nodata);
			buffer[i*3+0] = static_cast<uint8_t>((a[0]*w1 + b[0]*w2) / (w1+w2));
			buffer[i*3+1] = static_cast<uint8_t>((a[1]*w1 + b[1]*w2) / (w1+w2));
			buffer[i*3+2] = static_cast<uint8_t>((a[2]*w1 + b[2]*w2) / (w1+w2));
			buffer[i*3+3] = static_cast<uint8_t>((a[3]*w1 + b[3]*w2) / (w1+w2));
		}
	} else {
		throw std::runtime_error("not supported yet.");
	}
}

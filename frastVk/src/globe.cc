
#include "vk/app.h"
#include "vk/clipmap1/clipmap1.h"


int main() {

	VkApp app;
	//ClipMapRenderer1 cm(&app);
	//cm.init();

	while (not app.isDone()) {
		bool proc = app.pollWindowEvents();
		//if (proc) app.render();
		app.render();
		//usleep(33'000);
	}

	return 0;
}

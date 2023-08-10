echo " - Copying files"

mkdir subprojects || true;
cd subprojects
git clone https://github.com/ocornut/imgui --single-branch --depth 1 --branch v1.89.8 || true;
git clone https://github.com/epezent/implot --single-branch --depth 1 --branch v0.15 || true;
cd ../

mkdir frast2/frastgl/core/imgui/generated || true;
mkdir frast2/frastgl/core/implot/generated || true;
cp subprojects/imgui/imgui.cpp frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/imgui_draw.cpp frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/imgui_tables.cpp frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/imgui_widgets.cpp frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/imconfig.h frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/imgui_demo.cpp frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/imgui.h frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/imgui_internal.h frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/imstb_textedit.h frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/imstb_truetype.h frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/imstb_rectpack.h frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/backends/imgui_impl_glfw.h frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/backends/imgui_impl_opengl3.h frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/backends/imgui_impl_opengl3_loader.h frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/backends/imgui_impl_opengl3.cpp frast2/frastgl/core/imgui/generated/
cp subprojects/imgui/backends/imgui_impl_glfw.cpp frast2/frastgl/core/imgui/generated/

cp subprojects/implot/implot.cpp frast2/frastgl/core/implot/generated
cp subprojects/implot/implot_demo.cpp frast2/frastgl/core/implot/generated
cp subprojects/implot/implot.h frast2/frastgl/core/implot/generated
cp subprojects/implot/implot_internal.h frast2/frastgl/core/implot/generated
cp subprojects/implot/implot_items.cpp frast2/frastgl/core/implot/generated
echo " - Copying files ... done"

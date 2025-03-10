#include "subcommand.hpp"
#include <iostream>
#include "odgi.hpp"
#include "args.hxx"
#include <omp.h>
#include "algorithms/xp.hpp"
#include "algorithms/draw.hpp"
#include "algorithms/layout.hpp"
#include "utils.hpp"

namespace odgi {

using namespace odgi::subcommand;

int main_draw(int argc, char **argv) {

    // trick argumentparser to do the right thing with the subcommand
    for (uint64_t i = 1; i < argc - 1; ++i) {
        argv[i] = argv[i + 1];
    }
    std::string prog_name = "odgi draw";
    argv[0] = (char *) prog_name.c_str();
    --argc;

    args::ArgumentParser parser(
        "Draw previously-determined 2D layouts of the graph with diverse annotations.");
    args::Group mandatory_opts(parser, "[ MANDATORY OPTIONS ]");
    args::ValueFlag<std::string> dg_in_file(mandatory_opts, "FILE", "Load the succinct variation graph in ODGI format from this *FILE*. The file name usually ends with *.og*. It also accepts GFAv1, but the on-the-fly conversion to the ODGI format requires additional time!", {'i', "idx"});
    args::ValueFlag<std::string> layout_in_file(mandatory_opts, "FILE", "Read the layout coordinates from this .lay format FILE produced by odgi layout.", {'c', "coords-in"});
    //args::Flag in_is_tsv(parser, "is-tsv", "if the input is .tsv format (three column: id, X, Y) rather the default .lay binary format", {'I', "input-is-tsv"});
    args::Group files_io_opts(parser, "[ Files IO ]");
    args::ValueFlag<std::string> tsv_out_file(files_io_opts, "FILE", "Write the TSV layout plus displayed annotations to this FILE.", {'T', "tsv"});
    args::ValueFlag<std::string> svg_out_file(files_io_opts, "FILE", "Write an SVG rendering to this FILE.", {'s', "svg"});
    args::ValueFlag<std::string> png_out_file(files_io_opts, "FILE", "Write a rasterized PNG rendering to this FILE.", {'p', "png"});
    args::Group visualizations_opts(parser, "[ Visualization Options ]");
    args::ValueFlag<uint64_t> png_height(visualizations_opts, "FILE", "Height of PNG rendering (default: 1000).", {'H', "png-height"});
    args::ValueFlag<uint64_t> png_border(visualizations_opts, "FILE", "Size of PNG border in bp (default: 10).", {'E', "png-border"});
    args::Flag color_paths(visualizations_opts, "color-paths", "Color paths (in PNG output).", {'C', "color-paths"});
    args::ValueFlag<double> render_scale(visualizations_opts, "N", "Image scaling (default 1.0).", {'R', "scale"});
    args::ValueFlag<double> render_border(visualizations_opts, "N", "Image border (in approximate bp) (default 100.0).", {'B', "border"});
    args::ValueFlag<double> png_line_width(visualizations_opts, "N", "Line width (in approximate bp) (default 0.0).", {'w', "line-width"});
    //args::ValueFlag<double> png_line_overlay(parser, "N", "line width (in approximate bp) (default 10.0)", {'O', "line-overlay"});
    args::ValueFlag<double> png_path_line_spacing(visualizations_opts, "N", "Spacing between path lines in PNG layout (in approximate bp) (default 0.0).", {'S', "path-line-spacing"});
    args::ValueFlag<std::string> xp_in_file(files_io_opts, "FILE", "Load the path index from this FILE.", {'X', "path-index"});
	args::Group threading(parser, "[ Threading ]");
	args::ValueFlag<uint64_t> nthreads(threading, "N", "Number of threads to use for parallel operations.", {'t', "threads"});
	args::Group processing_info_opts(parser, "[ Processing Information ]");
	args::Flag progress(processing_info_opts, "progress", "Write the current progress to stderr.", {'P', "progress"});
    args::Group program_info_opts(parser, "[ Program Information ]");
    args::HelpFlag help(program_info_opts, "help", "Print a help message for odgi draw.", {'h', "help"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help) {
        std::cout << parser;
        return 0;
    } catch (args::ParseError e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    if (argc == 1) {
        std::cout << parser;
        return 1;
    }

    if (!dg_in_file) {
        std::cerr
            << "[odgi::draw] error: please specify an input file from where to load the graph via -i=[FILE], --idx=[FILE]."
            << std::endl;
        return 1;
    }

	if (!layout_in_file) {
		std::cerr
				<< "[odgi::draw] error: please specify an input file from where to load the layout from via -c=[FILE], --coords-in=[FILE]."
				<< std::endl;
		return 1;
	}

    if (!tsv_out_file && !svg_out_file && !png_out_file) {
        std::cerr
            << "[odgi::draw] error: please specify an output file to where to store the layout via -p/--png=[FILE], -s/--svg=[FILE], -T/--tsv=[FILE]"
            << std::endl;
        return 1;
    }

	const uint64_t num_threads = args::get(nthreads) ? args::get(nthreads) : 1;

	graph_t graph;
    assert(argc > 0);
    {
        const std::string infile = args::get(dg_in_file);
        if (!infile.empty()) {
            if (infile == "-") {
                graph.deserialize(std::cin);
            } else {
                utils::handle_gfa_odgi_input(infile, "draw", args::get(progress), num_threads, graph);
            }
        }
    }

    const uint64_t _png_height = png_height ? args::get(png_height) : 1000;
    const double _png_line_width = png_line_width ? args::get(png_line_width) : 0;
    const bool _color_paths = args::get(color_paths);
    const double _png_path_line_spacing = png_path_line_spacing ? args::get(png_path_line_spacing) : 0.0;
    const double svg_scale = !render_scale ? 1.0 : args::get(render_scale);
    uint64_t max_node_depth = 0;
    graph.for_each_handle(
        [&](const handle_t& h) {
            max_node_depth = std::max(graph.get_step_count(h), max_node_depth);
        });
    const double border_bp = !render_border ? std::max(100.0, _png_line_width * max_node_depth) : args::get(render_border);

    algorithms::layout::Layout layout;
    
    if (layout_in_file) {
        auto& infile = args::get(layout_in_file);
        if (!infile.empty()) {
            if (infile == "-") {
                layout.load(std::cin);
            } else {
                ifstream f(infile.c_str());
                layout.load(f);
                f.close();
            }
        }
    }

    if (tsv_out_file) {
        auto& outfile = args::get(tsv_out_file);
        if (!outfile.empty()) {
            if (outfile == "-") {
                layout.to_tsv(std::cout);
            } else {
                ofstream f(outfile.c_str());
                layout.to_tsv(f);
                f.close();
            }
        }
    }

    if (svg_out_file) {
        auto& outfile = args::get(svg_out_file);
        ofstream f(outfile.c_str());
        // todo could be done with callbacks
        std::vector<double> X = layout.get_X();
        std::vector<double> Y = layout.get_Y();
        algorithms::draw_svg(f, X, Y, graph, svg_scale, border_bp);
        f.close();    
    }

    if (png_out_file) {
        auto& outfile = args::get(png_out_file);
        // todo could be done with callbacks
        std::vector<double> X = layout.get_X();
        std::vector<double> Y = layout.get_Y();
        algorithms::draw_png(outfile, X, Y, graph, 1.0, border_bp, 0, _png_height, _png_line_width, _png_path_line_spacing, _color_paths);
    }
    
    return 0;
}

static Subcommand odgi_draw("draw", "Draw previously-determined 2D layouts of the graph with diverse annotations.",
                            PIPELINE, 3, main_draw);


}

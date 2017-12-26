#include "directional_light.h"

void qrcode::directional_light(igl::viewer::Viewer & viewer, Engine * engine, GLOBAL & global, Eigen::MatrixXd & verticles, Eigen::MatrixXi & facets)
{
	igl::Timer timer;

	/*Upper elevation and lower elevation*/

	const auto radian = [](float angle)->float {return angle / 180 * igl::PI; };

	Eigen::Vector3f upper_elevation, lower_elevation;

	upper_elevation << std::cos(radian(global.latitude_upper))*std::cos(radian(global.longitude)),
		std::cos(radian(global.latitude_upper))*std::sin(radian(global.longitude)),
		std::sin(radian(global.latitude_upper));

	lower_elevation << std::cos(radian(global.latitude_lower))*std::cos(radian(global.longitude)),
		std::cos(radian(global.latitude_lower))*std::sin(radian(global.longitude)),
		std::sin(radian(global.latitude_lower));


	/*model center*/
	int scale = global.info.scale;
	int border = global.info.border;
	int qr_size = (global.info.pixels.size() + 2 * border)*scale;

	Eigen::MatrixXi controller = global.under_control.block(scale, scale, qr_size, qr_size);
	std::vector<Eigen::RowVector4f> useful_point;

	for (int y = 0; y < qr_size - 2 * border*scale; y++) {
		for (int x = 0; x < qr_size - 2 * border*scale; x++) {
			if (controller(y + border*scale, x + border*scale) == 1) {
				Eigen::RowVector3d p = global.hit_matrix.row((y + border*scale)*(qr_size + 1) + x + border*scale);
				useful_point.push_back(Eigen::RowVector4f(p(0), p(1), p(2), 1));
			}
		}
	}

	Eigen::MatrixXf V(useful_point.size(), 4);

	for (int i = 0; i < useful_point.size(); i++)  V.row(i) << useful_point[i];

	V = (global.mode*(V.transpose())).transpose().block(0, 0, V.rows(), 3);

	Eigen::VectorXf centroid;
	centroid.setZero(3);

	for (int i = 0; i < V.rows(); i++)  centroid = (centroid.array() + V.row(i).transpose().array()).matrix();

	centroid = centroid / V.rows();

	/*Upper light source and lower light source*/

	Eigen::VectorXf upper_source(3), lower_source(3), upper_direct(3), lower_direct(3);

	upper_source = centroid + upper_elevation*global.zoom*global.distance;
	lower_source = centroid + lower_elevation*global.zoom*global.distance;

	Eigen::Matrix4f model = global.mode.inverse().eval();

	centroid.conservativeResize(4);
	centroid(3) = 1.f;
	centroid = (model*centroid).block(0, 0, 3, 1);

	upper_source.conservativeResize(4);
	upper_source(3) = 1.f;
	upper_source = (model*upper_source).block(0, 0, 3, 1);

	upper_direct = (centroid - upper_source).normalized();

	lower_source.conservativeResize(4);
	lower_source(3) = 1.f;
	lower_source = (model*lower_source).block(0, 0, 3, 1);

	lower_direct = (centroid - lower_source).normalized();

	/*upper field angle and lower field angle*/

	const auto upper_field_angle = [&radian, &upper_source, &upper_direct](Eigen::RowVectorXf &destination)->bool {
		Eigen::VectorXf d = (upper_source - destination.transpose()).normalized();
		return (std::sqrt(1.f - d.dot(upper_direct)*d.dot(upper_direct)) > std::sin(radian(5))) ? false : true;
	};

	const auto lower_field_angle = [&radian, &lower_source, &lower_direct](Eigen::RowVectorXf &destination)->bool {
		Eigen::VectorXf d = (lower_source - destination.transpose()).normalized();
		return (std::sqrt(1.f - d.dot(lower_direct)*d.dot(lower_direct)) > std::sin(radian(5))) ? false : true;
	};

	/*Merge meshes*/
	qrcode::find_hole(engine, global);
	qrcode::make_hole(global);
	qrcode::fix_hole(engine, global);

	verticles.resize(global.qr_verticals.rows() + global.rest_verticals.rows(), 3);
	verticles.block(0, 0, global.qr_verticals.rows(), 3) = global.qr_verticals;
	verticles.block(global.qr_verticals.rows(), 0, global.rest_verticals.rows(), 3) = global.rest_verticals;

	int size = global.qr_facets.rows() + global.rest_facets.rows();


	facets.resize(size, 3);
	facets.block(0, 0, global.qr_facets.rows(), 3) = global.qr_facets;
	facets.block(global.qr_facets.rows(), 0, global.rest_facets.rows(), 3) = global.rest_facets;

	for (int i = 0; i < global.component.size(); i++) {
		size = facets.rows();
		facets.conservativeResize(size + global.patches[i].rows(), 3);
		facets.block(size, 0, global.patches[i].rows(), 3) = global.patches[i];
	}




	/*Ambient occlusion visible region*/
	std::vector<Eigen::MatrixXi> modules = qrcode::module_adapter(engine, global);//pixel.size*scale+1;
	Eigen::MatrixXi both_modules = modules[0] + modules[1];

	Eigen::MatrixXi label;
	qrcode::bwlabel(engine, both_modules, 4, label);



	std::vector<Eigen::Vector3i> visible_info;
	for (int y = 0; y < label.rows(); y++)
		for (int x = 0; x < label.cols(); x++)
			if (label(y, x) != 0)
				visible_info.push_back(Eigen::Vector3i(y, x, label(y, x)));

	std::vector<Eigen::MatrixXi> visible_bound;
	qrcode::bwbound(label, visible_bound);
	std::vector<qrcode::SMesh> sphere_meshes = qrcode::visible_mesh_on_sphere(visible_info, visible_bound, global);

	std::cout << " Sphere mesh end" << std::endl;

	/*iterator step*/
	float step = 100000;
	for (int y = 0; y < modules[0].rows() - 1; y++) {
		for (int x = 0; x < modules[0].cols() - 1; x++) {
			float length = (global.hit_matrix.row(y*modules[0].cols() + x + 1) - global.hit_matrix.row(y*modules[0].cols() + x)).cast<float>().norm()
				/ abs(global.direct(y*modules[0].cols() + x, 2));
			if (length < step) step = length;
		}
	}

	step = step /0.1;




	/*Depth initialization*/
	global.carve_depth.setZero(global.qr_verticals.rows());
	Eigen::VectorXf depth;
	depth.setZero(global.anti_indicatior.size());

	for (int i = 0; i < global.anti_indicatior.size(); i++) {
		int y = global.anti_indicatior[i](0);
		int x = global.anti_indicatior[i](1);

		if (modules[0](y, x) == 1)
			depth(i) += step;

		else if (modules[1](y, x) == 1)
			depth(i) += step;
	}


	for (int i = 0; i < global.anti_indicatior.size(); i++) {
		int y = global.anti_indicatior[i](0);
		int x = global.anti_indicatior[i](1);


		if (modules[0](y, x) == 1)
			qrcode::patch(y, x, depth, both_modules, global);
		else if (modules[1](y, x) == 1)
			qrcode::patch(y, x, depth, both_modules, global);
	}

	Eigen::MatrixXd qr_verticals = global.qr_verticals;
	qrcode::carving_down(global, qr_verticals);

	verticles.block(0, 0, global.qr_verticals.rows(), 3) = qr_verticals;

	/*QR code normal and position*/
	Eigen::MatrixXf qr_position, qr_normal;
	qrcode::pre_pixel_normal(global, qr_verticals, qr_position, qr_normal);

	std::vector<Eigen::Vector3f> white_position, white_normal, upper_black_position, upper_black_normal, lower_black_position, lower_black_normal, both_black_position, both_black_normal;

	for (int i = 0; i < global.anti_indicatior.size(); i++) {
		int y = global.anti_indicatior[i](0);
		int x = global.anti_indicatior[i](1);

		if (modules[0](y, x) == 1) {
			upper_black_position.push_back(qr_position.row(i).transpose());
			upper_black_normal.push_back(qr_normal.row(i).transpose());
			both_black_position.push_back(qr_position.row(i).transpose());
			both_black_normal.push_back(qr_normal.row(i).transpose());

		}
		else if (modules[1](y, x) == 1) {
			lower_black_position.push_back(qr_position.row(i).transpose());
			lower_black_normal.push_back(qr_normal.row(i).transpose());
			both_black_position.push_back(qr_position.row(i).transpose());
			both_black_normal.push_back(qr_normal.row(i).transpose());
		}
		else {
			white_position.push_back(qr_position.row(i).transpose());
			white_normal.push_back(qr_normal.row(i).transpose());
		}
	}

	const auto light_to_gray = [](float a, float d)->int {
		return static_cast<int>(19.6*pow((0.87*(24 * a + 456 * d)), 0.3441) + 21.24);
	};

	Eigen::MatrixXf AO, DO_upper, DO_lower, DO;

	AO.setOnes(qr_size, qr_size);
	DO_upper.setOnes(qr_size, qr_size);
	DO_lower.setOnes(qr_size, qr_size);

	/*Test white region if lighted or not*/
	Eigen::Matrix<bool, Eigen::Dynamic, 1> white_condition;
	qrcode::light(verticles, facets, upper_source, white_position, white_condition);

	bool all_light = true;
	int index_white = 0;

	for (int i = 0; i < global.anti_indicatior.size(); i++) {
		int y = global.anti_indicatior[i](0);
		int x = global.anti_indicatior[i](1);

		if (modules[0](y, x) == 0 && modules[1](y, x) == 0) {

			if (y >= border*scale&&y < (qr_size - border*scale) && x >= border*scale&&x < (qr_size - border*scale)) {
				all_light &= white_condition(index_white);
			}
			index_white++;
		}
	}

	if (!all_light) {
		std::cout << "White modules can not be lighted!!" << std::endl;
		return;
	}

	std::vector<int> white_gray_value;

	Eigen::MatrixXi simu_gray_scale(qr_size, qr_size);
	simu_gray_scale.setConstant(255);

	Eigen::VectorXf white_AO;
	qrcode::ambient_occlusion(verticles, facets, white_position, white_normal, 500, white_AO);
	std::cout << "white ambient occlusion end" << std::endl;

	index_white = 0;
	for (int i = 0; i < global.anti_indicatior.size(); i++) {
		int y = global.anti_indicatior[i](0);
		int x = global.anti_indicatior[i](1);

		if (modules[0](y, x) == 0 && modules[1](y, x) == 0) {
			AO(y, x) = white_AO(index_white);

			Eigen::Vector3f dir = (upper_source - white_position[index_white]).normalized();
			DO_upper(y, x) = (white_condition(index_white) ? 1.f : 0.f)*dir.dot(white_normal[index_white]);
			DO_lower(y, x) = DO_upper(y, x);

			white_gray_value.push_back(light_to_gray(AO(y, x), DO_upper(y, x)));
			simu_gray_scale(y, x) = light_to_gray(AO(y, x), DO_upper(y, x));
			index_white++;
		}
	}

	std::sort(white_gray_value.begin(), white_gray_value.end(), [](int a, int b) {return a > b; });

	int top10 = ceil(white_gray_value.size() / 10);

	float white_average = 0;

	for (int i = 0; i < top10; i++) white_average += white_gray_value[i];

	white_average /= top10;




	/*Optimization*/




	bool should_stop = false;
	int iter_count = 0;

	while (!should_stop) {

		should_stop = true;

		Eigen::Matrix<bool, Eigen::Dynamic, 1> upper_black_condition, lower_black_condition, lower_black_upper_condition;

		qrcode::light(verticles, facets, upper_source, upper_black_position, upper_black_condition);
		qrcode::light(verticles, facets, lower_source, lower_black_position, lower_black_condition);
		qrcode::light(verticles, facets, lower_source, upper_black_position, lower_black_upper_condition);

		/*ambient simulation*/
		Eigen::VectorXf black_AO;
		qrcode::ambient_occlusion(verticles, facets, both_black_position, both_black_normal, sphere_meshes, black_AO);

		int index_upper_point = 0;
		int index_lower_point = 0;
		int index_both_point = 0;

		for (int i = 0; i < global.anti_indicatior.size(); i++) {
			int y = global.anti_indicatior[i](0);
			int x = global.anti_indicatior[i](1);

			if (modules[0](y, x) == 1) {

				Eigen::Vector3f upper_dir = (upper_source - upper_black_position[index_upper_point]).normalized();
				Eigen::Vector3f lower_dir = (lower_source - upper_black_position[index_upper_point]).normalized();

				AO(y, x) = black_AO(index_both_point) > 1 ? 1 : black_AO(index_both_point);

				DO_upper(y, x) = (upper_black_condition(index_upper_point) ? 1.f : 0.f)*upper_dir.dot(upper_black_normal[index_upper_point]);
				DO_lower(y, x) = (lower_black_upper_condition(index_upper_point) ? 1.f : 0.f)*lower_dir.dot(upper_black_normal[index_upper_point]);

				int gray_value = light_to_gray(AO(y, x), DO_upper(y, x));

				simu_gray_scale(y, x) = gray_value;

				if (gray_value > (white_average - 255 * 0.2f)) {

					depth(i) += step;
					should_stop = false;
				}

				index_upper_point++;
				index_both_point++;
			}
			else if (modules[1](y, x) == 1) {

				AO(y, x) = black_AO(index_both_point) > 1 ? 1 : black_AO(index_both_point);
				Eigen::Vector3f upper_dir = (upper_source - lower_black_position[index_lower_point]).normalized();
				Eigen::Vector3f lower_dir = (lower_source - lower_black_position[index_lower_point]).normalized();
				DO_upper(y, x) = 1.f*upper_dir.dot(lower_black_normal[index_lower_point]);
				DO_lower(y, x) = (lower_black_condition(index_lower_point) ? 1.f : 0.f)*lower_dir.dot(lower_black_normal[index_lower_point]);

				int gray_value = light_to_gray(AO(y, x), DO_lower(y, x));

				simu_gray_scale(y, x) = gray_value;

				if (gray_value > (white_average - 255 * 0.2f)) {

					depth(i) += step;
					should_stop = false;

				}

				index_lower_point++;
				index_both_point++;
			}

		}


		for (int i = 0; i < global.anti_indicatior.size(); i++) {
			int y = global.anti_indicatior[i](0);
			int x = global.anti_indicatior[i](1);

			if (modules[0](y, x) == 1)
				qrcode::patch(y, x, depth, both_modules, global);
			else if (modules[1](y, x) == 1)
				qrcode::patch(y, x, depth, both_modules, global);
		}

		qrcode::carving_down(global, qr_verticals);
		verticles.block(0, 0, global.qr_verticals.rows(), 3) = qr_verticals;

		qrcode::pre_pixel_normal(global, qr_verticals, qr_position, qr_normal);

		upper_black_position.clear();
		upper_black_normal.clear();
		lower_black_position.clear();
		lower_black_normal.clear();
		both_black_position.clear();
		both_black_normal.clear();

		for (int i = 0; i < global.anti_indicatior.size(); i++) {
			int y = global.anti_indicatior[i](0);
			int x = global.anti_indicatior[i](1);

			if (modules[0](y, x) == 1) {
				upper_black_position.push_back(qr_position.row(i).transpose());
				upper_black_normal.push_back(qr_normal.row(i).transpose());
				both_black_position.push_back(qr_position.row(i).transpose());
				both_black_normal.push_back(qr_normal.row(i).transpose());

			}
			else if (modules[1](y, x) == 1) {
				lower_black_position.push_back(qr_position.row(i).transpose());
				lower_black_normal.push_back(qr_normal.row(i).transpose());
				both_black_position.push_back(qr_position.row(i).transpose());
				both_black_normal.push_back(qr_normal.row(i).transpose());
			}
		}
		igl::writeOBJ("F:/3DQ/3DQR/data/Optimization/iter_" + std::to_string(iter_count) + ".obj", verticles, facets);
		qrcode::write_png("F:/3DQ/3DQR/data/Optimization/iter_" + std::to_string(iter_count) + ".png", simu_gray_scale);

		std::cout << "End of iterator:" << iter_count << std::endl;
		iter_count++;
	}

	int bound = global.info.pixels.size();

	for (int i = 0; i < global.black_module_segments.size(); i++) {

		Eigen::Vector3i segment = global.black_module_segments[i];
		int y = segment(0);
		int x = segment(1);
		int length = segment(2);


		if (length == 1) {
			int x_behind = x + length;
			for (int u = 0; u < scale; u++) {

				int origin_index = global.indicator[(y + border)*scale + u][(x + border)*scale + scale - 1](1);
				int end_index = global.indicator[(y + border)*scale + u][(x_behind + border)*scale + scale - 1](1);
				double origin_upper_point = qr_verticals(4 * origin_index + 2, 2);
				double origin_lower_point = qr_verticals(4 * origin_index + 3, 2);
				double end_upper_point = qr_verticals(4 * end_index + 2, 2);
				double end_lower_point = qr_verticals(4 * end_index + 3, 2);

				for (int v = 0; v < scale; v++) {
					int index = global.indicator[(y + border)*scale + u][(x_behind + border)*scale + v](1);
					double a = (scale - v)*(end_upper_point - origin_upper_point) / scale / abs(global.direct(((y + border)*scale + u)*(qr_size + 1) + (x_behind + border)*scale + v, 2));
					double b = (scale - v)*(end_lower_point - origin_lower_point) / scale / abs(global.direct(((y + border)*scale + u + 1)*(qr_size + 1) + (x_behind + border)*scale + v, 2));
					double c = (scale - (v + 1))*(end_upper_point - origin_upper_point) / scale / abs(global.direct(((y + border)*scale + u)*(qr_size + 1) + (x_behind + border)*scale + v + 1, 2));
					double d = (scale - (v + 1))*(end_upper_point - origin_upper_point) / scale / abs(global.direct(((y + border)*scale + u + 1)*(qr_size + 1) + (x_behind + border)*scale + v + 1, 2));
					qrcode::patch((y + border)*scale + u, (x_behind + border)*scale + v, global, Eigen::Vector4d(a, b, c, d));

				}
			}
		}
	}
	qrcode::carving_down(global, qr_verticals);
	verticles.block(0, 0, global.qr_verticals.rows(), 3) = qr_verticals;
	std::cout << "ok1" << std::endl;

	std::vector<qrcode::SMesh> appendix;

	std::vector<std::vector<Eigen::Matrix<int, 4, 3>>> mesh_info(qr_size);
	std::vector<std::vector<Eigen::Vector3i>>mesh_status(qr_size);
	Eigen::Matrix<int, 4, 3> info;
	info.setConstant(-1);

	for (int i = 0; i < qr_size; i++) {
		mesh_info[i].resize(qr_size, info);
		mesh_status[i].resize(qr_size, Eigen::Vector3i(0, 0, 0));
	}
		

	for (int y = 0; y < qr_size; y++)
	{
		for (int x = 0; x < qr_size; x++) {

			mesh_info[y][x](0, 0) = 4 * global.indicator[y][x](1);
			mesh_info[y][x](1, 0) = 4 * global.indicator[y][x](1) + 1;
			mesh_info[y][x](2, 0) = 4 * global.indicator[y][x](1) + 2;
			mesh_info[y][x](3, 0) = 4 * global.indicator[y][x](1) + 3;
		}
	}
	std::cout << "ok2" << std::endl;
	int row = 0;
	for (int i = 0; i < global.black_module_segments.size(); i++) {
		std::cout << "i" << std::endl;

		Eigen::Vector3i segment = global.black_module_segments[i];
		int y = segment(0);
		int x = segment(1);
		int length = segment(2);

		if (length > 1) {

			int temp_len = length;

			if ((x + length) == global.info.pixels.size()) temp_len = 4;

			Eigen::MatrixXd append_verticals(8 * scale*scale*temp_len, 3);
			std::vector<Eigen::Vector3i> append_facets;

			for (int u = 0; u < scale; u++) {

				int lower_index = global.indicator[(y + border)*scale + u][(x + length - 1 + border)*scale + scale - 1](1);
				int upper_index = global.indicator[(y + border)*scale + u][(x + length + border)*scale](1);

				double upper_left_point = qr_verticals(4 * upper_index, 2);
				double upper_right_point = qr_verticals(4 * upper_index + 1, 2);

				double lower_left_point = qr_verticals(4 * lower_index + 2, 2);
				double lower_right_point = qr_verticals(4 * lower_index + 3, 2);

				int seg_size = scale*temp_len;

				double diff_left = (lower_left_point - upper_left_point) / seg_size;
				double diff_right = (lower_right_point - upper_right_point) / seg_size;
				std::cout << "ok3" << std::endl;

				for (int v = 0; v < seg_size; v++) {
					int index_seg = u*seg_size + v;
					int index_seg_1 = (u + 1)*seg_size + v;

					int r = (y + border)*scale + u;
					int c = (x + length + border)*scale + v;

					//append_verticals.row(4 * index_seg) << global.hit_matrix(r*(qr_size + 1) + c,0), global.hit_matrix(r*(qr_size + 1) + c, 1),lower_left_point;
					//append_verticals.row(4 * index_seg + 1) <<global.hit_matrix((r + 1)*(qr_size + 1) + c,0), global.hit_matrix((r + 1)*(qr_size + 1) + c, 1),lower_right_point;
					//append_verticals.row(4 * index_seg + 2) <<global.hit_matrix(r*(qr_size + 1) + c + 1,0), global.hit_matrix(r*(qr_size + 1) + c + 1, 1), lower_left_point;
					//append_verticals.row(4 * index_seg + 3)<< global.hit_matrix((r + 1)*(qr_size + 1) + c + 1,0), global.hit_matrix((r + 1)*(qr_size + 1) + c + 1, 1),lower_right_point;

					append_verticals.row(4 * index_seg) =
						global.hit_matrix.row(r*(qr_size + 1) + c) + global.direct.row(r*(qr_size + 1) + c).cast<double>()*(lower_left_point - upper_left_point) / global.direct(r*(qr_size + 1) + c, 2);
					append_verticals.row(4 * index_seg + 1) =
						global.hit_matrix.row((r + 1)*(qr_size + 1) + c) + global.direct.row((r + 1)*(qr_size + 1) + c).cast<double>()*(lower_right_point - upper_right_point) / global.direct((r + 1)*(qr_size + 1) + c, 2);
					append_verticals.row(4 * index_seg + 2) =
						global.hit_matrix.row(r*(qr_size + 1) + c + 1) + global.direct.row(r*(qr_size + 1) + c + 1).cast<double>()*(lower_left_point - upper_left_point) / global.direct(r*(qr_size + 1) + c + 1, 2);
					append_verticals.row(4 * index_seg + 3) =
						global.hit_matrix.row((r + 1)*(qr_size + 1) + c + 1) + global.direct.row((r + 1)*(qr_size + 1) + c + 1).cast<double>()*(lower_right_point - upper_right_point) / global.direct((r + 1)*(qr_size + 1) + c + 1, 2);


					append_facets.emplace_back(4 * index_seg, 4 * index_seg + 1, 4 * index_seg + 2);
					append_facets.emplace_back(4 * index_seg + 1, 4 * index_seg + 3, 4 * index_seg + 2);

					



					/*append_verticals.row(4 * scale*seg_size + 4 * index_seg) <<
						global.hit_matrix(r*(qr_size + 1) + c, 0),
						global.hit_matrix(r*(qr_size + 1) + c, 1),
						upper_left_point + diff_right*v;
					append_verticals.row(4 * scale*seg_size + 4 * index_seg + 1) <<
						global.hit_matrix((r + 1)*(qr_size + 1) + c, 0),
						global.hit_matrix((r + 1)*(qr_size + 1) + c, 1),
						upper_right_point + diff_right*v;
					append_verticals.row(4 * scale*seg_size + 4 * index_seg + 2) <<
						global.hit_matrix(r*(qr_size + 1) + c + 1, 0),
						global.hit_matrix(r*(qr_size + 1) + c + 1, 1),
						upper_left_point + diff_left*(v+1);
					append_verticals.row(4 * scale*seg_size + 4 * index_seg + 3) <<
						global.hit_matrix((r + 1)*(qr_size + 1) + c + 1, 0),
						global.hit_matrix((r + 1)*(qr_size + 1) + c + 1, 1),
						upper_right_point + diff_right*(v+1);*/


					append_verticals.row(4 * scale*seg_size + 4 * index_seg) =
						global.hit_matrix.row(r*(qr_size + 1) + c) + global.direct.row(r*(qr_size + 1) + c).cast<double>()* v *diff_left / global.direct(r*(qr_size + 1) + c, 2);
					append_verticals.row(4 * scale*seg_size + 4 * index_seg + 1) =
						global.hit_matrix.row((r + 1)*(qr_size + 1) + c) + global.direct.row((r + 1)*(qr_size + 1) + c).cast<double>()* v *diff_right / global.direct((r + 1)*(qr_size + 1) + c, 2);
					append_verticals.row(4 * scale*seg_size + 4 * index_seg + 2) =
						global.hit_matrix.row(r*(qr_size + 1) + c + 1) + global.direct.row(r*(qr_size + 1) + c + 1).cast<double>()* (v + 1) *diff_left / global.direct(r*(qr_size + 1) + c + 1, 2);
					append_verticals.row(4 * scale*seg_size + 4 * index_seg + 3) =
						global.hit_matrix.row((r + 1)*(qr_size + 1) + c + 1) + global.direct.row((r + 1)*(qr_size + 1) + c + 1).cast<double>()* (v + 1) *diff_right / global.direct((r + 1)*(qr_size + 1) + c + 1, 2);


					append_facets.emplace_back(4 * scale*seg_size + 4 * index_seg, 4 * scale*seg_size + 4 * index_seg + 2, 4 * scale*seg_size + 4 * index_seg + 1);
					append_facets.emplace_back(4 * scale*seg_size + 4 * index_seg + 1, 4 * scale*seg_size + 4 * index_seg + 2, 4 * scale*seg_size + 4 * index_seg + 3);

					
					

					if (u == 0) {
						facets.row(global.anti_indicatior.size() * 2 + global.patch_indicator[(r - 1)*qr_size + c](3)) << 0, 0, 0;
						facets.row(global.anti_indicatior.size() * 2 + global.patch_indicator[r*qr_size + c](2)) << 0, 0, 0;

						mesh_info[r][c](0, 2) = verticles.rows() + row + 4 * index_seg;
						mesh_info[r][c](2, 2) = verticles.rows() + row + 4 * index_seg + 2;
						mesh_info[r][c](0, 1) = verticles.rows() + row + 4 * scale*seg_size + 4 * index_seg;
						mesh_info[r][c](2, 1) = verticles.rows() + row + 4 * scale*seg_size + 4 * index_seg + 2;

					}


					if (u == (scale - 1)) {
						facets.row(global.anti_indicatior.size() * 2 + global.patch_indicator[r*qr_size + c](3)) << 0, 0, 0;
						facets.row(global.anti_indicatior.size() * 2 + global.patch_indicator[(r + 1)*qr_size + c](2)) << 0, 0, 0;

						mesh_info[r][c](1, 2) = verticles.rows() + row + 4 * index_seg + 1;
						mesh_info[r][c](3, 2) = verticles.rows() + row + 4 * index_seg + 3;
						mesh_info[r][c](1, 1) = verticles.rows() + row + 4 * scale*seg_size + 4 * index_seg + 1;
						mesh_info[r][c](3, 1) = verticles.rows() + row + 4 * scale*seg_size + 4 * index_seg + 3;


					}
					if (v == 0) {
						facets.row(global.anti_indicatior.size() * 2 + global.patch_indicator[r*qr_size + c](0)) << 0, 0, 0;
						facets.row(global.anti_indicatior.size() * 2 + global.patch_indicator[r*qr_size + c - 1](1)) << 0, 0, 0;
					}

				}
			}
			std::cout << "ok4" << std::endl;

			for (int u = 0; u < scale; u++) {
				int seg_size = scale*temp_len;

				for (int v = 0; v < seg_size; v++) {
					int index_seg = u*seg_size + v;
					int index_seg_1 = (u + 1)*seg_size + v;
					int scope_index = 4 * scale*seg_size;

					double up_l = append_verticals(4 * index_seg + 1, 2) / 2 + append_verticals(4 * index_seg + 3, 2) / 2;
					double dw_l = append_verticals(4 * index_seg_1, 2) / 2 + append_verticals(4 * index_seg_1 + 2, 2) / 2;

					double up_h = append_verticals(scope_index + 4 * index_seg + 1, 2) / 2 + append_verticals(scope_index + 4 * index_seg + 3, 2) / 2;
					double dw_h = append_verticals(scope_index + 4 * index_seg_1, 2) / 2 + append_verticals(scope_index + 4 * index_seg_1 + 2, 2) / 2;

					

					if (u < scale - 1) {

						if (up_l > dw_l) {
							if (up_l > dw_h) {
								append_facets.push_back(Eigen::Vector3i(scope_index + 4 * index_seg_1, 4 * index_seg_1, scope_index + 4 * index_seg_1 + 2));
								append_facets.push_back(Eigen::Vector3i(scope_index + 4 * index_seg_1 + 2, 4 * index_seg_1, 4 * index_seg_1 + 2));
							}
							else {
								append_facets.push_back(Eigen::Vector3i(4 * index_seg + 1, 4 * index_seg_1, 4 * index_seg + 3));
								append_facets.push_back(Eigen::Vector3i(4 * index_seg + 3, 4 * index_seg_1, 4 * index_seg_1 + 2));
							}
						}
						if (up_l < dw_l) {//right
							if (up_h < dw_l) {
								append_facets.push_back(Eigen::Vector3i(4 * index_seg + 1, scope_index + 4 * index_seg + 1,4 * index_seg + 3));
								append_facets.push_back(Eigen::Vector3i(4 * index_seg + 3, scope_index + 4 * index_seg + 1, scope_index + 4 * index_seg + 3));
							}
							else {
								append_facets.push_back(Eigen::Vector3i(4 * index_seg + 1, 4 * index_seg_1, 4 * index_seg + 3));
								append_facets.push_back(Eigen::Vector3i(4 * index_seg + 3, 4 * index_seg_1, 4 * index_seg_1 + 2));
							}

						}
						/*if (up_l = dw_l) {
							append_facets.emplace_back(4 * index_seg + 1, 4 * index_seg_1, 4 * index_seg + 3);
							append_facets.emplace_back(4 * index_seg + 3, 4 * index_seg_1, 4 * index_seg_1 + 2);
						}*/
					}


					if (u < scale - 1) {


						if (up_h > dw_h) {
							if (up_l > dw_h) {//right
								append_facets.push_back(Eigen::Vector3i(4 * index_seg + 1, scope_index + 4 * index_seg + 1, 4 * index_seg + 3));
								append_facets.push_back(Eigen::Vector3i(4 * index_seg + 3, scope_index + 4 * index_seg + 1, scope_index + 4 * index_seg + 3));
							}
							else {
								append_facets.push_back(Eigen::Vector3i(scope_index + 4 * index_seg_1, scope_index + 4 * index_seg+1, scope_index + 4 * index_seg_1 + 2));
								append_facets.push_back(Eigen::Vector3i(scope_index + 4 * index_seg_1 + 2, scope_index + 4 * index_seg+1, scope_index + 4 * index_seg + 3));
							}

						}
						if (up_h < dw_h) {
							if (up_h < dw_l) {
								append_facets.push_back(Eigen::Vector3i(scope_index + 4 * index_seg_1, 4 * index_seg_1, scope_index + 4 * index_seg_1 + 2));
								append_facets.push_back(Eigen::Vector3i(scope_index + 4 * index_seg_1 + 2, 4 * index_seg_1, 4 * index_seg_1 + 2));
							}
							else {//invert
								append_facets.push_back(Eigen::Vector3i(scope_index + 4 * index_seg_1, scope_index + 4 * index_seg + 1,  scope_index + 4 * index_seg_1 + 2));
								append_facets.push_back(Eigen::Vector3i(scope_index + 4 * index_seg_1 + 2, scope_index + 4 * index_seg+1, scope_index + 4 * index_seg + 3));
							}
						}
						/*if (up_h = dw_h) {
							append_facets.emplace_back(scope_index + 4 * index_seg + 1, scope_index + 4 * index_seg_1, scope_index + 4 * index_seg + 3);
							append_facets.emplace_back(scope_index + 4 * index_seg + 3, scope_index + 4 * index_seg_1, scope_index + 4 * index_seg_1 + 2);
						}*/

					}

				}
			}
			std::cout << "ok5" << std::endl;
			Eigen::MatrixXi f(append_facets.size(), 3);
			for (int j = 0; j < append_facets.size(); j++) f.row(j)= append_facets[j].transpose();
			appendix.push_back({ append_verticals, f });
			row += append_verticals.rows();
		}

	}

	std::cout << "ok6" << std::endl;
	for (int i = 0; i < appendix.size(); i++) {
		int v_row = verticles.rows();
		int f_row = facets.rows();

		verticles.conservativeResize(v_row + appendix[i].V.rows(), 3);
		verticles.block(v_row, 0, appendix[i].V.rows(), 3) = appendix[i].V;
		facets.conservativeResize(f_row + appendix[i].F.rows(), 3);
		facets.block(f_row, 0, appendix[i].F.rows(), 3) = (appendix[i].F.array() + v_row).matrix();
	}
	std::cout << "end of block 1" << std::endl;
	std::vector<Eigen::Vector3i> face;

	for (int i = 0; i < global.black_module_segments.size(); i++) {

		Eigen::Vector3i segment = global.black_module_segments[i];
		int y = segment(0);
		int x = segment(1);
		int length = segment(2);

		if (length > 1) {

			int temp_len = length;

			if ((x + length) == global.info.pixels.size()) temp_len = 4;

			for (int u = 0; u < scale; u++) {

				int seg_size = scale*temp_len;

				for (int v = 0; v < seg_size; v++) {

					int r = (y + border)*scale + u;
					int c = (x + length + border)*scale + v;


						if (u == 0) {
							double height_std = (verticles(mesh_info[r][c](0, 1), 2) + verticles(mesh_info[r][c](2, 1), 2)) / 2;
							//upper
							//if (mesh_status[r][c][0] == 0) {

							if (mesh_info[r - 1][c](1, 2) != -1 && mesh_info[r - 1][c](3, 2) != -1 && mesh_info[r - 1][c](1, 1) != -1 && mesh_info[r - 1][c](3, 1) != -1) {

								if ((verticles(mesh_info[r - 1][c](1, 2), 2) / 2 + verticles(mesh_info[r - 1][c](3, 2), 2) / 2) > height_std) {
									if ((verticles(mesh_info[r - 1][c](1, 1), 2) / 2 + verticles(mesh_info[r - 1][c](3, 1), 2) / 2) >= (verticles(mesh_info[r][c](0, 0), 2) / 2 + verticles(mesh_info[r][c](2, 0), 2) / 2)) {
										face.emplace_back(mesh_info[r - 1][c](1, 2), mesh_info[r][c](0, 0), mesh_info[r - 1][c](3, 2));
										face.emplace_back(mesh_info[r - 1][c](3, 2), mesh_info[r][c](0, 0), mesh_info[r][c](2, 0));
									}
									
									//mesh_status[r][c](0) = 1;
									//mesh_status[r - 1][c](0) = 1;
								}

								else {
									if ((verticles(mesh_info[r - 1][c](1, 0), 2) / 2 + verticles(mesh_info[r - 1][c](3, 0), 2) / 2) > height_std &&
										(verticles(mesh_info[r - 1][c](1, 1), 2) / 2 + verticles(mesh_info[r - 1][c](3, 1), 2) / 2) < (verticles(mesh_info[r][c](0, 0), 2) / 2 + verticles(mesh_info[r][c](2, 0), 2) / 2)) {
										face.emplace_back(mesh_info[r - 1][c](1, 0), mesh_info[r][c](0, 0), mesh_info[r - 1][c](3, 0));
										face.emplace_back(mesh_info[r - 1][c](3, 0), mesh_info[r][c](0, 0), mesh_info[r][c](2, 0));
										//mesh_status[r][c](0) = 1;
										//mesh_status[r - 1][c](0) = 1;
									}
									else {
										face.emplace_back(mesh_info[r][c](0, 1), mesh_info[r][c](0, 0), mesh_info[r][c](2, 1));
										face.emplace_back(mesh_info[r][c](2, 1), mesh_info[r][c](0, 0), mesh_info[r][c](2, 0));
									}
								}
							}
							else {

								/*	if ((verticles(mesh_info[r - 1][c](1, 0), 2) / 2 + verticles(mesh_info[r - 1][c](3, 0), 2) / 2) > height_std &&
								(verticles(mesh_info[r][c](0,0),2)/2+verticles(mesh_info[r-1][c](2,0)/2))>(verticles(mesh_info[r - 1][c](1, 1), 2) / 2 + verticles(mesh_info[r - 1][c](3, 1), 2) / 2)
								) {
								face.emplace_back(mesh_info[r - 1][c](1, 0), mesh_info[r][c](0, 0), mesh_info[r - 1][c](3, 0));
								face.emplace_back(mesh_info[r - 1][c](3, 0), mesh_info[r][c](0, 0), mesh_info[r][c](2, 0));
								//mesh_status[r][c](0) = 1;
								//mesh_status[r - 1][c](0) = 1;
								}*/
								if ((verticles(mesh_info[r - 1][c](1, 0), 2) / 2 + verticles(mesh_info[r - 1][c](3, 0), 2) / 2) < height_std) {
									face.emplace_back(mesh_info[r][c](0, 1), mesh_info[r][c](0, 0), mesh_info[r][c](2, 1));
									face.emplace_back(mesh_info[r][c](2, 1), mesh_info[r][c](0, 0), mesh_info[r][c](2, 0));
									//mesh_status[r][c](0) = 1;
								}
								else {
									face.emplace_back(mesh_info[r - 1][c](1, 0), mesh_info[r][c](0, 0), mesh_info[r - 1][c](3, 0));
									face.emplace_back(mesh_info[r - 1][c](3, 0), mesh_info[r][c](0, 0), mesh_info[r][c](2, 0));
								}

								/*if ((verticles(mesh_info[r - 1][c](1, 0), 2) / 2 + verticles(mesh_info[r - 1][c](3, 0), 2) / 2) > height_std) {
									face.emplace_back(mesh_info[r - 1][c](1, 0), mesh_info[r][c](0, 0), mesh_info[r - 1][c](3, 0));
									face.emplace_back(mesh_info[r - 1][c](3, 0), mesh_info[r][c](0, 0), mesh_info[r][c](2, 0));
								}

								if ((verticles(mesh_info[r][c](0, 0), 2) / 2 + verticles(mesh_info[r][c](2, 0) / 2)) < (verticles(mesh_info[r - 1][c](1, 1), 2) / 2 + verticles(mesh_info[r - 1][c](3, 1), 2) / 2) &&
									(verticles(mesh_info[r][c](0, 1), 2) / 2 + verticles(mesh_info[r][c](2, 1) / 2)) > (verticles(mesh_info[r - 1][c](1, 2), 2) / 2 + verticles(mesh_info[r - 1][c](3, 2), 2) / 2)
									) {
									face.emplace_back(mesh_info[r][c](0, 1), mesh_info[r][c](0, 0), mesh_info[r][c](2, 1));
									face.emplace_back(mesh_info[r][c](2, 1), mesh_info[r][c](0, 0), mesh_info[r][c](2, 0));
								}*/

							}
						
							

							

							
							//}

							//middle

							//if (mesh_status[r][c](1) == 0) {

							if (mesh_info[r - 1][c](1, 1) != -1 && mesh_info[r - 1][c](3, 1) != -1&& mesh_info[r][c](0, 1) != -1 && mesh_info[r][c](2, 1) != -1) {

								double max = std::max((verticles(mesh_info[r - 1][c](1, 1), 2) + verticles(mesh_info[r - 1][c](3, 1), 2)) / 2, (verticles(mesh_info[r][c](0, 1), 2) + verticles(mesh_info[r][c](2, 1), 2)) / 2);
								double min = std::min((verticles(mesh_info[r - 1][c](1, 1), 2) + verticles(mesh_info[r - 1][c](3, 1), 2)) / 2, (verticles(mesh_info[r][c](0, 1), 2) + verticles(mesh_info[r][c](2, 1), 2)) / 2);
						
								double up_0 = (verticles(mesh_info[r - 1][c](1, 0), 2) + verticles(mesh_info[r - 1][c](3, 0), 2)) / 2;
								double up_2 = (verticles(mesh_info[r - 1][c](1, 2), 2) + verticles(mesh_info[r - 1][c](3, 2), 2)) / 2;
								double down_0 = (verticles(mesh_info[r][c](0, 0), 2) + verticles(mesh_info[r][c](2, 0), 2)) / 2;
								double down_2 = (verticles(mesh_info[r][c](0, 2), 2) + verticles(mesh_info[r][c](2, 2), 2)) / 2;

								if (!((up_0<max&&up_0>min) || (up_2<max&&up_2>min) || (down_0<max&&down_0>min) || (down_2<max&&down_2>min))) {
									face.emplace_back( mesh_info[r][c](0, 1),mesh_info[r - 1][c](1, 1), mesh_info[r - 1][c](3, 1));
									face.emplace_back( mesh_info[r][c](0, 1),mesh_info[r - 1][c](3, 1), mesh_info[r][c](2, 1));
									//mesh_status[r][c](1) = 1;
									//mesh_status[r - 1][c](1) = 1;
								}
							}
							//}

							//lower

							//if (mesh_status[r][c](2) == 0) {

							double height_std_l = (verticles(mesh_info[r][c](0, 2), 2) + verticles(mesh_info[r][c](2, 2), 2)) / 2;

							if (mesh_info[r - 1][c](1, 2) != -1 && mesh_info[r - 1][c](3, 2) != -1&& mesh_info[r - 1][c](1, 1) != -1 && mesh_info[r - 1][c](3, 1) != -1) {
								if ((verticles(mesh_info[r - 1][c](1, 2), 2) / 2 + verticles(mesh_info[r - 1][c](3, 2), 2) / 2) > height_std_l &&
									(verticles(mesh_info[r - 1][c](1, 2), 2) / 2 + verticles(mesh_info[r - 1][c](3, 2), 2) / 2) < height_std) {
									face.emplace_back(mesh_info[r - 1][c](1, 2), mesh_info[r][c](0, 2), mesh_info[r - 1][c](3, 2));
									face.emplace_back(mesh_info[r - 1][c](3, 2), mesh_info[r][c](0, 2), mesh_info[r][c](2, 2));
									//mesh_status[r][c](2) = 1;
									//mesh_status[r - 1][c](2) = 1;
								}
								else if ((verticles(mesh_info[r - 1][c](1, 2), 2) / 2 + verticles(mesh_info[r - 1][c](3, 2), 2) / 2) > height_std_l &&
									(verticles(mesh_info[r - 1][c](1, 2), 2) / 2 + verticles(mesh_info[r - 1][c](3, 2), 2) / 2) > height_std) {

									face.emplace_back(mesh_info[r][c](0, 1), mesh_info[r][c](0, 2), mesh_info[r][c](2, 1));
									face.emplace_back(mesh_info[r][c](2, 1), mesh_info[r][c](0, 2), mesh_info[r][c](2, 2));
									//mesh_status[r][c](2) = 1;

								}
								if ((verticles(mesh_info[r - 1][c](1, 1), 2) / 2 + verticles(mesh_info[r - 1][c](3, 1), 2) / 2) < height_std_l &&
									(verticles(mesh_info[r - 1][c](1, 0), 2) / 2 + verticles(mesh_info[r - 1][c](3, 0), 2) / 2) > height_std) {
									face.emplace_back(mesh_info[r][c](0, 1), mesh_info[r][c](0, 2), mesh_info[r][c](2, 1));
									face.emplace_back(mesh_info[r][c](2, 1), mesh_info[r][c](0, 2), mesh_info[r][c](2, 2));
								}
								/*else if ((verticles(mesh_info[r - 1][c](1, 1), 2) / 2 + verticles(mesh_info[r - 1][c](3, 2), 2) / 2) > height_std_l &&
									(verticles(mesh_info[r - 1][c](1, 2), 2) / 2 + verticles(mesh_info[r - 1][c](3, 2), 2) / 2) < height_std){
									face.emplace_back(mesh_info[r - 1][c](1, 1), mesh_info[r][c](0, 2), mesh_info[r - 1][c](3, 1));
									face.emplace_back(mesh_info[r - 1][c](3, 1), mesh_info[r][c](0, 2), mesh_info[r][c](2, 2));
									}*/
								}
								else{
									if ((verticles(mesh_info[r - 1][c](1, 0), 2) / 2 + verticles(mesh_info[r - 1][c](3, 0), 2) / 2) < height_std) {

										face.emplace_back(mesh_info[r - 1][c](1,0), mesh_info[r][c](0, 2), mesh_info[r - 1][c](3, 0));
										face.emplace_back(mesh_info[r - 1][c](3, 0), mesh_info[r][c](0, 2), mesh_info[r][c](2, 2));
									}
									else {
										face.emplace_back(mesh_info[r][c](0, 1), mesh_info[r][c](0, 2), mesh_info[r][c](2, 1));
										face.emplace_back(mesh_info[r][c](2, 1), mesh_info[r][c](0, 2), mesh_info[r][c](2, 2));
									}
								}
							//}

						}


					if (u == (scale - 1)) {
						double height_std = (verticles(mesh_info[r][c](1, 1), 2) + verticles(mesh_info[r][c](3, 1), 2)) / 2;

						//upper
						//if (mesh_status[r][c][0] == 0) {

						if (mesh_info[r + 1][c](0, 2) != -1 && mesh_info[r + 1][c](2, 2) != -1 && mesh_info[r + 1][c](0, 1) != -1 && mesh_info[r + 1][c](2, 1) != -1) {

							if ((verticles(mesh_info[r + 1][c](0, 2), 2) / 2 + verticles(mesh_info[r + 1][c](2, 2), 2) / 2) > height_std) {
								if ((verticles(mesh_info[r + 1][c](0, 1), 2) / 2 + verticles(mesh_info[r + 1][c](2, 1), 2) / 2) >= (verticles(mesh_info[r][c](1, 0), 2) / 2 + verticles(mesh_info[r][c](3, 0), 2) / 2)) {
									face.emplace_back(mesh_info[r][c](1, 0), mesh_info[r + 1][c](0, 2), mesh_info[r][c](3, 0));
									face.emplace_back(mesh_info[r][c](3, 0), mesh_info[r + 1][c](0, 2), mesh_info[r + 1][c](2, 2));
								}
								
								//mesh_status[r][c](0) = 1;
								//mesh_status[r + 1][c](0) = 1;
							}

							else {
								if ((verticles(mesh_info[r + 1][c](0, 0), 2) / 2 + verticles(mesh_info[r + 1][c](2, 0), 2) / 2) > height_std
									&& (verticles(mesh_info[r + 1][c](0, 1), 2) / 2 + verticles(mesh_info[r + 1][c](2, 1), 2) / 2) < (verticles(mesh_info[r][c](1, 0), 2) / 2 + verticles(mesh_info[r][c](3, 0), 2) / 2)) {
									face.emplace_back(mesh_info[r][c](1, 0), mesh_info[r + 1][c](0, 0), mesh_info[r][c](3, 0));
									face.emplace_back(mesh_info[r][c](3, 0), mesh_info[r + 1][c](0, 0), mesh_info[r + 1][c](2, 0));
									//mesh_status[r][c](0) = 1;
									//mesh_status[r + 1][c](0) = 1;
								}
								else {
									face.emplace_back(mesh_info[r][c](1, 0), mesh_info[r][c](1, 1), mesh_info[r][c](3, 0));
									face.emplace_back(mesh_info[r][c](3, 0), mesh_info[r][c](1, 1), mesh_info[r][c](3, 1));
								}

							}
						}
						else {
							/*if ((verticles(mesh_info[r + 1][c](0, 0), 2) / 2 + verticles(mesh_info[r + 1][c](2, 0), 2) / 2) > height_std&&
							(verticles(mesh_info[r][c](1,0),2)/2+verticles(mesh_info[r][c](3,0),2)/2)>(verticles(mesh_info[r + 1][c](0, 1), 2) / 2 + verticles(mesh_info[r + 1][c](2, 1), 2) / 2)
							) {
							face.emplace_back(mesh_info[r][c](1, 0), mesh_info[r + 1][c](0, 0), mesh_info[r][c](3, 0));
							face.emplace_back(mesh_info[r][c](3, 0), mesh_info[r + 1][c](0, 0), mesh_info[r + 1][c](2, 0));
							//mesh_status[r][c](0) = 1;
							//mesh_status[r + 1][c](0) = 1;
							}*/

							if ((verticles(mesh_info[r + 1][c](0, 0), 2) / 2 + verticles(mesh_info[r + 1][c](2, 0), 2) / 2) < height_std) {
								face.emplace_back(mesh_info[r][c](1, 0), mesh_info[r][c](1, 1), mesh_info[r][c](3, 0));
								face.emplace_back(mesh_info[r][c](3, 0), mesh_info[r][c](1, 1), mesh_info[r][c](3, 1));
								//mesh_status[r][c](0) = 1;
							}
							else {
								face.emplace_back(mesh_info[r][c](1, 0), mesh_info[r + 1][c](0, 0), mesh_info[r][c](3, 0));
								face.emplace_back(mesh_info[r][c](3, 0), mesh_info[r + 1][c](0, 0), mesh_info[r + 1][c](2, 0));
							}

							/*if ((verticles(mesh_info[r + 1][c](0, 0), 2) / 2 + verticles(mesh_info[r + 1][c](2, 0), 2) / 2) > height_std) {
								face.emplace_back(mesh_info[r][c](1, 0), mesh_info[r + 1][c](0, 0), mesh_info[r][c](3, 0));
								face.emplace_back(mesh_info[r][c](3, 0), mesh_info[r + 1][c](0, 0), mesh_info[r + 1][c](2, 0));
								//mesh_status[r][c](0) = 1;
							}

							if ((verticles(mesh_info[r][c](1, 0), 2) / 2 + verticles(mesh_info[r][c](3, 0), 2) / 2) < (verticles(mesh_info[r + 1][c](0, 1), 2) / 2 + verticles(mesh_info[r + 1][c](2, 1), 2) / 2) &&
								(verticles(mesh_info[r][c](1, 1), 2) / 2 + verticles(mesh_info[r][c](3, 1), 2) / 2) > (verticles(mesh_info[r + 1][c](0, 2), 2) / 2 + verticles(mesh_info[r + 1][c](2, 2), 2) / 2)) {
								face.emplace_back(mesh_info[r][c](1, 0), mesh_info[r][c](1, 1), mesh_info[r][c](3, 0));
								face.emplace_back(mesh_info[r][c](3, 0), mesh_info[r][c](1, 1), mesh_info[r][c](3, 1));
							}*/

							//}
						}

						//middle

						//if (mesh_status[r][c](1) == 0) {


						if (mesh_info[r + 1][c](0, 1) != -1 && mesh_info[r + 1][c](2, 1) != -1&& mesh_info[r][c](1, 1) != -1 && mesh_info[r][c](3, 1) != -1) {

							double max = std::max((verticles(mesh_info[r][c](1, 1), 2) + verticles(mesh_info[r][c](3, 1), 2)) / 2, (verticles(mesh_info[r+1][c](0, 1), 2) + verticles(mesh_info[r+1][c](2, 1), 2)) / 2);
							double min = std::min((verticles(mesh_info[r][c](1, 1), 2) + verticles(mesh_info[r][c](3, 1), 2)) / 2, (verticles(mesh_info[r+1][c](0, 1), 2) + verticles(mesh_info[r+1][c](2, 1), 2)) / 2);

							double up_0 = (verticles(mesh_info[r][c](1, 0), 2) + verticles(mesh_info[r][c](3, 0), 2)) / 2;
							double up_2 = (verticles(mesh_info[r][c](1, 2), 2) + verticles(mesh_info[r][c](3, 2), 2)) / 2;
							double down_0 = (verticles(mesh_info[r+1][c](0, 0), 2) + verticles(mesh_info[r+1][c](2, 0), 2)) / 2;
							double down_2 = (verticles(mesh_info[r+1][c](0, 2), 2) + verticles(mesh_info[r+1][c](2, 2), 2)) / 2;

							if (!((up_0<max&&up_0>min) || (up_2<max&&up_2>min) || (down_0<max&&down_0>min) || (down_2<max&&down_2>min))) {
								face.emplace_back(mesh_info[r + 1][c](0, 1),mesh_info[r][c](1, 1),  mesh_info[r][c](3, 1));
								face.emplace_back(mesh_info[r + 1][c](0, 1),mesh_info[r][c](3, 1), mesh_info[r + 1][c](2, 1));
								//mesh_status[r][c](1) = 1;
								//mesh_status[r - 1][c](1) = 1;
							}
						}
						//}

						//lower

						//if (mesh_status[r][c](2) == 0) {

						double height_std_l = (verticles(mesh_info[r][c](1, 2), 2) + verticles(mesh_info[r][c](3, 2), 2)) / 2;

						if (mesh_info[r + 1][c](0, 2) != -1 && mesh_info[r + 1][c](2, 2) != -1 && mesh_info[r + 1][c](0, 1) != -1 && mesh_info[r + 1][c](2, 1) != -1) {
							if ((verticles(mesh_info[r + 1][c](0, 2), 2) / 2 + verticles(mesh_info[r + 1][c](2, 2), 2) / 2) > height_std_l &&
								(verticles(mesh_info[r + 1][c](0, 2), 2) / 2 + verticles(mesh_info[r + 1][c](2, 2), 2) / 2) < height_std) {
								face.emplace_back(mesh_info[r][c](1, 2), mesh_info[r + 1][c](0, 2), mesh_info[r][c](3, 2));
								face.emplace_back(mesh_info[r][c](3, 2), mesh_info[r + 1][c](0, 2), mesh_info[r + 1][c](2, 2));
								//mesh_status[r][c](2) = 1;
								//mesh_status[r + 1][c](2) = 1;
							}
							else if ((verticles(mesh_info[r + 1][c](0, 2), 2) / 2 + verticles(mesh_info[r + 1][c](2, 2), 2) / 2) > height_std_l &&
								(verticles(mesh_info[r + 1][c](0, 2), 2) / 2 + verticles(mesh_info[r + 1][c](0, 2), 2) / 2) > height_std) {

								face.emplace_back( mesh_info[r][c](1, 2),mesh_info[r][c](1, 1), mesh_info[r][c](3, 1));
								face.emplace_back( mesh_info[r][c](1, 2),mesh_info[r][c](3, 1), mesh_info[r][c](3, 2));
								//mesh_status[r][c](2) = 1;

							}
							if ((verticles(mesh_info[r + 1][c](0, 1), 2) / 2 + verticles(mesh_info[r + 1][c](2, 1), 2) / 2) < height_std_l &&
								(verticles(mesh_info[r + 1][c](0, 0), 2) / 2 + verticles(mesh_info[r + 1][c](2, 0), 2) / 2) > height_std) {
								face.emplace_back(mesh_info[r][c](1, 2), mesh_info[r][c](1, 1), mesh_info[r][c](3, 1));
								face.emplace_back(mesh_info[r][c](1, 2), mesh_info[r][c](3, 1), mesh_info[r][c](3, 2));
							}
							/*else if ((verticles(mesh_info[r + 1][c](1, 1), 2) / 2 + verticles(mesh_info[r + 1][c](3, 2), 2) / 2) > height_std_l &&
							(verticles(mesh_info[r + 1][c](1, 2), 2) / 2 + verticles(mesh_info[r + 1][c](3, 2), 2) / 2) < height_std){
							face.emplace_back(mesh_info[r + 1][c](1, 1), mesh_info[r][c](0, 2), mesh_info[r + 1][c](3, 1));
							face.emplace_back(mesh_info[r + 1][c](3, 1), mesh_info[r][c](0, 2), mesh_info[r][c](2, 2));
							}*/
						}
						else {
							if ((verticles(mesh_info[r + 1][c](0, 0), 2) / 2 + verticles(mesh_info[r + 1][c](2, 0), 2) / 2) < height_std) {

								face.emplace_back(mesh_info[r][c](1, 2), mesh_info[r + 1][c](0, 0), mesh_info[r][c](3, 2));
								face.emplace_back(mesh_info[r][c](3, 2), mesh_info[r + 1][c](0, 0), mesh_info[r + 1][c](2, 0));
							}
							else {
								face.emplace_back( mesh_info[r][c](1, 2),mesh_info[r][c](1, 1), mesh_info[r][c](3, 1));
								face.emplace_back( mesh_info[r][c](1, 2),mesh_info[r][c](3, 1), mesh_info[r][c](3, 2));
							}
						}
						//}


					}

				}
			}

		}
		
	}

	Eigen::MatrixXi add_facets(face.size(), 3);
	for (int i = 0; i < face.size(); i++) add_facets.row(i) = face[i];
	int origin_row = facets.rows();
	facets.conservativeResize(origin_row + add_facets.rows(), 3);
	facets.block(origin_row, 0, add_facets.rows(), 3) = add_facets;
	std::cout << "end of block 2" << std::endl;
	std::vector<Eigen::Vector3i> face_vec;
	for (int i = 0; i < facets.rows(); i++) face_vec.push_back(facets.row(i));
	std::sort(face_vec.begin(), face_vec.end(), [](Eigen::Vector3i v1, Eigen::Vector3i v2) {
		int a, b;
		int max1 = v1.maxCoeff(&a);
		int min1 = v1.minCoeff(&b);
		int mid1;
		for (int g = 0; g < 3; g++)
			if (g != a&&g != b) mid1 = v1[g];

		int max2 = v2.maxCoeff(&a);
		int min2 = v2.minCoeff(&b);
		int mid2;
		for (int g = 0; g < 3; g++)
			if (g != a&&g != b) mid2 = v2[g];

		if(max1<max2) return true;
		if (max1 > max2) return false;
		if (max1 == max2) {
			if(mid1<mid2) return true;
			if(mid1>mid2) return false;
			if (mid1 == mid2) {
				if(min1<=min2) return true;
				if (min1 > min2) return false;
			}
		}

	});
	face_vec.erase(std::unique(face_vec.begin(), face_vec.end(), [](Eigen::Vector3i v1,Eigen::Vector3i v2) {
		int a, b;
		int max1 = v1.maxCoeff(&a);
		int min1 = v1.minCoeff(&b);
		int mid1;
		for (int g = 0; g < 3; g++)
			if (g != a&&g != b) mid1 = v1[g];

		int max2 = v2.maxCoeff(&a);
		int min2 = v2.minCoeff(&b);
		int mid2;
		for (int g = 0; g < 3; g++)
			if (g != a&&g != b) mid2 = v2[g];

		return((max1 == max2) && (mid1 == mid2) && (min1 == min2));
	
	}), face_vec.end());

	facets.resize(face_vec.size(), 3);
	for (int i = 0; i < face_vec.size(); i++) facets.row(i) = face_vec[i];


	igl::writeOBJ("F:/3DQ/3DQR/data/Optimization/final_model.obj", verticles, facets);
}



#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include "httplib.h" 

using namespace std;


// Struktur data diperbarui untuk mengakomodasi warna merah yang terbelah di 2 rentang Hue
struct ObjekFisik {
    string nama;
    cv::Scalar lowerHLS;
    cv::Scalar upperHLS;
    cv::Scalar warnaBox;
    float W_real; 
    bool isRed;           // Penanda apakah objek ini butuh 2 rentang warna (khusus merah) <Error untuk merah, jadi minta saran gemini>
    cv::Scalar lowerHLS2;     // Batas bawah HLS kedua
    cv::Scalar upperHLS2;     // Batas atas HLS kedua
};

cv::Mat frame_terbaru;
mutex mtx;

int main() {
    cout << "=== MEMUAT PARAMETER KALIBRASI ===" << endl;
    cv::FileStorage fs("yaml/kalibrasi.yaml", cv::FileStorage::READ);
    if (!fs.isOpened()) {
        cerr << "Error: Tidak dapat membuka yaml/kalibrasi.yaml! Pastikan terminal berada di folder utama proyek." << endl;
        return -1;
    }

    cv::Mat cameraMatrix;
    fs["camera_matrix"] >> cameraMatrix;
    fs.release();

    double fx = cameraMatrix.at<double>(0, 0);
    double fy = cameraMatrix.at<double>(1, 1);
    double cx = cameraMatrix.at<double>(0, 2);
    double cy = cameraMatrix.at<double>(1, 2);

    //Saya lupa yang disuruh bikin web itu mas brenanda atau mas hendra, jadi bikin 2 2nya deh
    httplib::Server svr;

    svr.Get("/", [](const httplib::Request &, httplib::Response &res) {
        string html = R"(
            <!DOCTYPE html>
            <html>
            <head>
                <title>IRIS ITS - KPP Week 2</title>
                <style>
                    body { background-color: #121212; color: #ffffff; font-family: 'Segoe UI', sans-serif; text-align: center; padding: 20px; }
                    h1 { color: #00d2ff; margin-bottom: 5px; }
                    h3 { color: #aaaaaa; margin-top: 0; font-weight: normal; }
                    .video-container { margin-top: 20px; border: 4px solid #00d2ff; display: inline-block; padding: 5px; border-radius: 12px; background: #000; box-shadow: 0 0 20px rgba(0, 210, 255, 0.4); }
                    img { border-radius: 8px; max-width: 100%; height: auto; }
                    .footer { margin-top: 30px; color: #666; font-size: 14px; }
                </style>
            </head>
            <body>
                <h1>IRIS ITS ROBOTICS</h1>
                <h3>Estimasi Jarak 3D & Segmentasi Warna</h3>
                <div class="video-container">
                    <img src="/video_feed" alt="Video Feed OpenCV" />
                </div>
                <div class="footer">Real-time Stream via C++ (cpp-httplib)</div>
            </body>
            </html>
        )";
        res.set_content(html, "text/html");
    });

    svr.Get("/video_feed", [&](const httplib::Request &, httplib::Response &res) {
        res.set_content_provider(
            "multipart/x-mixed-replace; boundary=frame",
            [&](size_t offset, httplib::DataSink &sink) {
                while (true) {
                    vector<uchar> buffer;
                    {
                        lock_guard<mutex> lock(mtx);
                        if (frame_terbaru.empty()) {
                            this_thread::sleep_for(chrono::milliseconds(10));
                            continue;
                        }
                        imencode(".jpg", frame_terbaru, buffer);
                    }
                    
                    string header = "--frame\r\nContent-Type: image/jpeg\r\n\r\n";
                    if (!sink.write(header.data(), header.size())) break;
                    if (!sink.write((const char*)buffer.data(), buffer.size())) break;
                    if (!sink.write("\r\n", 2)) break;
                    
                    this_thread::sleep_for(chrono::milliseconds(30));
                }
                return true;
            }
        );
    });

    thread server_thread([&]() {
        svr.listen("0.0.0.0", 8080);
    });
    
    cout << "\n>>> WEB SERVER BERHASIL DINYALAKAN! <<<" << endl;
    cout << ">>> BUKA BROWSER: http://localhost:8080 <<<\n" << endl;

    // OPENCV TRACKING & JARAK 3D
    vector<ObjekFisik> daftarObjek = {
        {{"Tas Merah", cv::Scalar(0, 30, 120), cv::Scalar(7, 255, 255), cv::Scalar(0, 0, 255), 12, true, cv::Scalar(165, 30, 120), cv::Scalar(179, 255, 255)},
        {"Tas Biru", cv::Scalar(100, 50, 50), cv::Scalar(130, 255, 255), cv::Scalar(255, 0, 0), 16.2, false, cv::Scalar(), cv::Scalar()},
        {"Tas Hijau", cv::Scalar(40, 50, 50), cv::Scalar(80, 255, 255), cv::Scalar(0, 255, 0), 21.9, false, cv::Scalar(), cv::Scalar()}
    }};

    // IP, aku ga bisa pakai 2 jadi aku pakai IP awalku
    cv::VideoCapture cap("http://10.170.46.120:4747/video", cv::CAP_FFMPEG);
    if (!cap.isOpened()) {
        cerr << "Error: Kamera DroidCam tidak dapat dibuka!" << endl;
        svr.stop();
        server_thread.join();
        return -1;
    }

    cv::Mat frame, hlsImage;
    double t = 0; 
    int fps = 0;

    while (true) {
        t = (double)cv::getTickCount();
        cap >> frame;
        if (frame.empty()) break;

        cvtColor(frame, hlsImage, cv::COLOR_BGR2HLS);

        for (const auto& objek : daftarObjek) {
            cv::Mat mask, mask1, mask2;
            
            // Proses khusus untuk warna merah yang butuh 2 rentang agar deteksinya tidak putus
            if (objek.isRed) {
                inRange(hlsImage, objek.lowerHLS, objek.upperHLS, mask1);
                inRange(hlsImage, objek.lowerHLS2, objek.upperHLS2, mask2);
                bitwise_or(mask1, mask2, mask); // Gabungkan kedua topeng warna merah
            } else {
                inRange(hlsImage, objek.lowerHLS, objek.upperHLS, mask);
            }

            cv::erode(mask, mask, cv::Mat(), cv::Point(-1, -1), 2);
            cv::dilate(mask, mask, cv::Mat(), cv::Point(-1, -1), 2);

            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

            double maxArea = 0;
            int largestContourIdx = -1;
            for (int i = 0; i < contours.size(); i++) {
                double area = contourArea(contours[i]);
                if (area > 1000 && area > maxArea) { 
                    maxArea = area;
                    largestContourIdx = i;
                }
            }

            if (largestContourIdx != -1) {
                cv::Rect boundingBox = boundingRect(contours[largestContourIdx]);
                cv::rectangle(frame, boundingBox, objek.warnaBox, 2);

                double x_pixel = boundingBox.x + (boundingBox.width / 2.0);
                double y_pixel = boundingBox.y + (boundingBox.height / 2.0);
                cv::circle(frame, cv::Point(x_pixel, y_pixel), 5, objek.warnaBox, cv::FILLED);

                double Z = (fx * objek.W_real) / boundingBox.width;
                double X = ((x_pixel - cx) * Z) / fx;
                double Y = ((y_pixel - cy) * Z) / fy;

                string teksNama = objek.nama;
                string teksZ = cv::format("Z: %.1f cm", Z);
                string teksXYZ = cv::format("(%.1f, %.1f, %.1f)", X, Y, Z);

                cv::Point ptTeks(boundingBox.x, boundingBox.y - 45);
                cv::putText(frame, teksNama, ptTeks, cv::FONT_HERSHEY_SIMPLEX, 0.6, objek.warnaBox, 2);
                cv::putText(frame, teksZ, cv::Point(ptTeks.x, ptTeks.y + 20), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
                cv::putText(frame, teksXYZ, cv::Point(ptTeks.x, ptTeks.y + 40), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
            }
        }

        t = ((double)cv::getTickCount() - t) / cv::getTickFrequency();
        fps = 1.0 / t;
        cv::putText(frame, cv::format("FPS: %d", fps), cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
        cv::putText(frame, "Tekan 'C' untuk Screencute | 'ESC' Keluar", cv::Point(20, 70), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);

        {
            lock_guard<mutex> lock(mtx);
            frame_terbaru = frame.clone();
        }

        imshow("Deteksi OpenCV", frame);
        
        char key = (char)cv::   waitKey(30);
        if (key == 27) {
            break;
        } else if (key == 'c' || key == 'C') {
            cv::imwrite("docs/objek_detect.png", frame);
            cout << "Screenshot HUD berhasil disimpan ke 'docs/objek_detect.png'!" << endl;
            cv::Mat whiteFlash = cv::Mat(frame.size(), frame.type(), cv::Scalar(255, 255, 255));
            cv::imshow("Deteksi OpenCV", whiteFlash);
            cv::waitKey(150);
        }
    }

    cap.release();
    cv::destroyAllWindows();
    svr.stop();
    server_thread.join();

    return 0;
}
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <string>

using namespace std;
using namespace cv;

int main() {
    // 1. Konfigurasi Papan Catur
    int boardWidth = 9;  
    int boardHeight = 6; 
    Size boardSize(boardWidth, boardHeight);
    float squareSize = 2.5f; 

    // Buka DroidCam (Ganti IP sesuai di HP-mu)
    VideoCapture cap("http://10.78.159.48:4747/video", CAP_FFMPEG); 
    if (!cap.isOpened()) {
        cerr << "Error: Kamera tidak dapat dibuka!" << endl;
        return -1;
    }

    Mat frame, gray;
    int imgCount = 0;
    int targetPhotos = 25; 

    vector<vector<Point3f>> objectPoints;
    vector<vector<Point2f>> imagePoints;

    vector<Point3f> obj;
    for (int i = 0; i < boardHeight; i++) {
        for (int j = 0; j < boardWidth; j++) {
            obj.push_back(Point3f(j * squareSize, i * squareSize, 0.0f));
        }
    }

    cout << "=== PROGRAM KALIBRASI & SCREENSHOT ===" << endl;
    cout << "Arahkan kamera ke papan catur. Tunggu garis warna-warni muncul." << endl;
    cout << "Tekan 'C' untuk memotret dan menyimpan (Target: 25)." << endl;

    // 2. Sesi Pengambilan 25 Screenshot Fisik
    while (imgCount < targetPhotos) {
        cap >> frame;
        if (frame.empty()) break;

        Mat display = frame.clone(); // Mat terpisah agar screenshot yang disimpan tetap bersih tanpa coretan
        cvtColor(frame, gray, COLOR_BGR2GRAY);

        vector<Point2f> corners;
        // Proses deteksi pola catur secara REAL-TIME
        bool found = findChessboardCorners(gray, boardSize, corners, 
            CALIB_CB_ADAPTIVE_THRESH | CALIB_CB_NORMALIZE_IMAGE | CALIB_CB_FAST_CHECK);

        // Jika ketemu, gambar garis warna-warninya di layar
        if (found) {
            drawChessboardCorners(display, boardSize, corners, found);
        }

        // Teks HUD
        putText(display, format("Screenshot tersimpan: %d / %d", imgCount, targetPhotos), 
                Point(20, 40), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 0), 2);
        
        if(found) {
            putText(display, "Pola Terkunci! Tekan 'C' sekarang", Point(20, 70), 
                    FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 255, 0), 2);
        } else {
            putText(display, "Mencari pola... Geser/Arahkan kamera", Point(20, 70), 
                    FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 255), 2);
        }

        imshow("Kamera Uji Coba", display);

        char key = (char)waitKey(30);
        if (key == 'q' || key == 27) {
            break;
        } 
        else if (key == 'c' || key == 'C') {
            if (found) {
                // Perhalus titik koordinat
                cornerSubPix(gray, corners, Size(11, 11), Size(-1, -1), 
                    TermCriteria(TermCriteria::EPS + TermCriteria::COUNT, 30, 0.1));
                
                imagePoints.push_back(corners);
                objectPoints.push_back(obj);

                imgCount++;
                
                // Simpan gambar BERSIH (tanpa coretan garis) ke dalam folder untuk bukti tugas
                string filename = "screenshot_uji_coba_" + to_string(imgCount) + ".jpg";
                imwrite(filename, frame); 

                cout << "Berhasil menyimpan: " << filename << endl;
                
                // Efek kedip layar
                bitwise_not(display, display);
                imshow("Kamera Uji Coba", display);
                waitKey(200);
            } else {
                cout << "Peringatan: Pola belum terdeteksi! Jangan tekan C dulu." << endl;
            }
        }
    }
    
    destroyAllWindows();
    cap.release();

    // 3. Proses Kalibrasi (Otomatis setelah 25 screenshot valid terkumpul)
    if (imgCount == targetPhotos) {
        cout << "\n=== 25 FOTO TERKUMPUL! MEMULAI PERHITUNGAN MATRIKS ===" << endl;
        
        Mat cameraMatrix = Mat::eye(3, 3, CV_64F);
        Mat distCoeffs = Mat::zeros(8, 1, CV_64F);
        vector<Mat> rvecs, tvecs;

        double rms = calibrateCamera(objectPoints, imagePoints, frame.size(), 
                                     cameraMatrix, distCoeffs, rvecs, tvecs);
                                     
        cout << "Kalibrasi Selesai! Nilai RMS Error: " << rms << endl;

        FileStorage fs("yaml/kalibrasi.yaml", FileStorage::WRITE);
        if (fs.isOpened()) {
            fs << "camera_matrix" << cameraMatrix;
            fs << "distortion_coefficients" << distCoeffs;
            fs.release();
            cout << "SUCCESS: File berhasil dibuat di 'yaml/kalibrasi.yaml'" << endl;
        } else {
            cerr << "ERROR: Gagal menyimpan! Pastikan folder 'yaml/' sudah ada di foldermu." << endl;
        }
    } else {
        cout << "Dibatalkan. Foto kurang dari 25." << endl;
    }

    return 0;
}

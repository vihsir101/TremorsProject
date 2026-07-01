using Microsoft.Win32.SafeHandles;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;

namespace CalmHandsWPF
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    /// 


    public partial class MainWindow : Window
    {
        private const uint GENERIC_READ = 0x80000000;
        private const uint GENERIC_WRITE = 0x40000000;
        private const uint OPEN_EXISTING = 3;
        private const int MAX_TAPS = 64;

        // Same IOCTL_UPDATE_FIR_SETTINGS in driver code but bit shifted
        private const uint IOCTL_UPDATE_FIR_SETTINGS = (0x0000000f << 16) | (0 << 14) | (0x801 << 2) | 0;


        // Import CreateFile and DeviceIoControl from Windows
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
        private static extern SafeFileHandle CreateFile(
            string lpFileName, uint dwDesiredAccess, uint dwShareMode,
            IntPtr lpSecurityAttributes, uint dwCreationDisposition,
            uint dwFlagsAndAttributes, IntPtr hTemplateFile);


        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool DeviceIoControl(
            SafeFileHandle hDevice, uint dwIoControlCode,
            ref FIR_SETTINGS lpInBuffer, int nInBufferSize,
            IntPtr lpOutBuffer, int nOutBufferSize,
            out int lpBytesReturned, IntPtr lpOverlapped);



        [StructLayout(LayoutKind.Sequential, Pack = 4)]
        public struct FIR_SETTINGS
        {
            public int NumTapsX;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = MAX_TAPS)]
            public int[] CoefficientsX;

            public int NumTapsY;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = MAX_TAPS)]
            public int[] CoefficientsY;
        }


        public MainWindow()
        {

            InitializeComponent();

            // Set Slider Values to last saved
            XFreqSlider.Value = Settings.Default.SavedXFreq;
            YFreqSlider.Value = Settings.Default.SavedYFreq;

        }

        // When the SaveSettings Buttion is clicked
        private void SaveSettings_Click(object sender, RoutedEventArgs e)
        {
            // Save the values to Settings.settings so they don't get deleted if user closes app
            CalmHandsWPF.Settings.Default.SavedXFreq = XFreqSlider.Value;
            CalmHandsWPF.Settings.Default.SavedYFreq = YFreqSlider.Value;
            CalmHandsWPF.Settings.Default.Save();

            // Initialize the number of taps in filter and frequency of mouse
            // TODO: Use driver to automatically find sampling frequency
            int numTaps = 31;
            double samplingFrequency = 125.0;

            // Get both slider values as a integer, cutoff frequency for filters
            int cutOffX = (int)Math.Round(XFreqSlider.Value);
            int cutOffY = (int)Math.Round(YFreqSlider.Value);

            // Using CreateFilter, find the best coefficents for both filters in order cut out the frequency
            int[] coeffX = CreateFilter(numTaps, cutOffX, samplingFrequency);
            int[] coeffY = CreateFilter(numTaps, cutOffY, samplingFrequency);

            // Send the coefficents to the driver
            bool success = SendToDriver(numTaps, coeffX, numTaps, coeffY);


            int Error = 0;
            if (!success) {
                Error = Marshal.GetLastWin32Error();
            }

            // If there is no error, block the apply changes button do user can't misclick it
            // If there is a errr, leave it open
            SaveSettings.IsEnabled = success;

        }

        private void Slider_ValueChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            // Unblock apply changes button if slider was adjusted
            if (SaveSettings != null)
            {
                SaveSettings.IsEnabled = true;
            }
        }

        private int[] CreateFilter(int taps, double cutoff, double sampleRate)
        {
            double[] rawCoeffs = new double[taps];
            int[] scaledCoeffs = new int[MAX_TAPS];

            double fc = cutoff / sampleRate;
            int M = taps - 1;
            double sum = 0.0;

            for (int i = 0; i < taps; i++)
            {
                if (i == M / 2.0) rawCoeffs[i] = 2.0 * fc;
                else
                {
                    double n = i - M / 2.0;
                    rawCoeffs[i] = Math.Sin(2.0 * Math.PI * fc * n) / (Math.PI * n);
                }

                double hamming = 0.54 - 0.46 * Math.Cos(2.0 * Math.PI * i / M);
                rawCoeffs[i] *= hamming;
                sum += rawCoeffs[i];
            }

            for (int i = 0; i < taps; i++) rawCoeffs[i] /= sum;

            int integerSum = 0;
            for (int i = 0; i < taps; i++)
            {
                scaledCoeffs[i] = (int)Math.Round(rawCoeffs[i] * 1024.0);
                integerSum += scaledCoeffs[i];
            }

            int remainder = 1024 - integerSum;
            scaledCoeffs[taps / 2] += remainder;

            return scaledCoeffs;
        }
        private bool SendToDriver(int tapsX, int[] coeffsX, int tapsY, int[] coeffsY)
        {
            using (SafeFileHandle driverHandle = CreateFile(
                @"\\.\CalmHandsLink",
                GENERIC_READ | GENERIC_WRITE,
                0,
                IntPtr.Zero,
                OPEN_EXISTING,
                0,
                IntPtr.Zero))
            {
                if (driverHandle.IsInvalid) return false;

                FIR_SETTINGS settings = new FIR_SETTINGS{NumTapsX = tapsX, CoefficientsX = coeffsX, NumTapsY = tapsY, CoefficientsY = coeffsY};

                int bytesReturned = 0;

                return DeviceIoControl(
                    driverHandle,
                    IOCTL_UPDATE_FIR_SETTINGS,
                    ref settings,
                    Marshal.SizeOf(settings),
                    IntPtr.Zero,
                    0,
                    out bytesReturned,
                    IntPtr.Zero
                );
            }
        }
    }
}
namespace SmoothFontCreator;

partial class MainForm
{
   /// <summary>
   ///  Required designer variable.
   /// </summary>
   private System.ComponentModel.IContainer components = null;

   /// <summary>
   ///  Clean up any resources being used.
   /// </summary>
   /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
   protected override void Dispose(bool disposing)
   {
      if (disposing && (components != null))
      {
         components.Dispose();
      }
      base.Dispose(disposing);
   }

   #region Windows Form Designer generated code

   /// <summary>
   ///  Required method for Designer support - do not modify
   ///  the contents of this method with the code editor.
   /// </summary>
   private void InitializeComponent()
   {
      tabControl1 = new TabControl();
      tabPage1 = new TabPage();
      showLineSeparatorsCheckBox = new CheckBox();
      PreviewTextBox = new TextBox();
      groupBox9 = new GroupBox();
      VLWCharPanel = new Panel();
      groupBox5 = new GroupBox();
      vlwAscentTextBox = new TextBox();
      vlwDescentTextBox = new TextBox();
      label15 = new Label();
      label16 = new Label();
      groupBox6 = new GroupBox();
      label19 = new Label();
      vlwWidthTextBox = new TextBox();
      label22 = new Label();
      vlwHeightTextBox = new TextBox();
      label21 = new Label();
      gdXTextBox = new TextBox();
      label20 = new Label();
      gdYTextBox = new TextBox();
      gxAdvanceTextBox = new TextBox();
      label18 = new Label();
      paddingTextBox = new TextBox();
      label17 = new Label();
      groupBox8 = new GroupBox();
      groupBox2 = new GroupBox();
      label25 = new Label();
      ttDescentLabel = new Label();
      label26 = new Label();
      label1 = new Label();
      ttAscentLabel = new Label();
      ttLineSpacingTextBox = new TextBox();
      ttDescentTextBox = new TextBox();
      ttSizeTextBox = new TextBox();
      ttHeightTextBox = new TextBox();
      ttAscentTextBox = new TextBox();
      groupBox7 = new GroupBox();
      label24 = new Label();
      label2 = new Label();
      label23 = new Label();
      label31 = new Label();
      ttCharWidthTextBox = new TextBox();
      ttCharHeightTextBox = new TextBox();
      ttCellWidthTextBox = new TextBox();
      label9 = new Label();
      ttCellHeightTextBox = new TextBox();
      label37 = new Label();
      TrueTypeCharPanel = new Panel();
      label29 = new Label();
      label28 = new Label();
      label27 = new Label();
      TrueTypeTextPanel = new Panel();
      VLWTextPanel = new Panel();
      HexLabel = new Label();
      label5 = new Label();
      TestCharTextBox = new TextBox();
      tabPage2 = new TabPage();
      panel1 = new Panel();
      groupBox4 = new GroupBox();
      label14 = new Label();
      digitCheckBox = new CheckBox();
      label32 = new Label();
      label6 = new Label();
      label4 = new Label();
      label30 = new Label();
      label3 = new Label();
      verticalPaddingUpDown = new NumericUpDown();
      horizontalPaddingUpDown = new NumericUpDown();
      label7 = new Label();
      cellHeightUpDown = new NumericUpDown();
      label8 = new Label();
      monospaceCheckBox = new CheckBox();
      groupBox3 = new GroupBox();
      label13 = new Label();
      label12 = new Label();
      label11 = new Label();
      boldCheckBox = new CheckBox();
      FontComboBox = new ComboBox();
      italicCheckBox = new CheckBox();
      SaveButton = new Button();
      label10 = new Label();
      fontSizesTextBox = new TextBox();
      statusTextBox = new TextBox();
      testButton1 = new Button();
      testButton2 = new Button();
      testButton3 = new Button();
      button1 = new Button();
      label33 = new Label();
      magnificationLabel = new Label();
      tabControl1.SuspendLayout();
      tabPage1.SuspendLayout();
      groupBox9.SuspendLayout();
      groupBox5.SuspendLayout();
      groupBox6.SuspendLayout();
      groupBox8.SuspendLayout();
      groupBox2.SuspendLayout();
      groupBox7.SuspendLayout();
      tabPage2.SuspendLayout();
      groupBox4.SuspendLayout();
      ((System.ComponentModel.ISupportInitialize)verticalPaddingUpDown).BeginInit();
      ((System.ComponentModel.ISupportInitialize)horizontalPaddingUpDown).BeginInit();
      ((System.ComponentModel.ISupportInitialize)cellHeightUpDown).BeginInit();
      groupBox3.SuspendLayout();
      SuspendLayout();
      // 
      // tabControl1
      // 
      tabControl1.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      tabControl1.Controls.Add(tabPage1);
      tabControl1.Controls.Add(tabPage2);
      tabControl1.Location = new Point(12, 12);
      tabControl1.Name = "tabControl1";
      tabControl1.SelectedIndex = 0;
      tabControl1.Size = new Size(2293, 1199);
      tabControl1.TabIndex = 0;
      // 
      // tabPage1
      // 
      tabPage1.Controls.Add(magnificationLabel);
      tabPage1.Controls.Add(label33);
      tabPage1.Controls.Add(showLineSeparatorsCheckBox);
      tabPage1.Controls.Add(PreviewTextBox);
      tabPage1.Controls.Add(groupBox9);
      tabPage1.Controls.Add(groupBox8);
      tabPage1.Controls.Add(label29);
      tabPage1.Controls.Add(label28);
      tabPage1.Controls.Add(label27);
      tabPage1.Controls.Add(TrueTypeTextPanel);
      tabPage1.Controls.Add(VLWTextPanel);
      tabPage1.Controls.Add(HexLabel);
      tabPage1.Controls.Add(label5);
      tabPage1.Controls.Add(TestCharTextBox);
      tabPage1.Location = new Point(4, 34);
      tabPage1.Name = "tabPage1";
      tabPage1.Padding = new Padding(6);
      tabPage1.Size = new Size(2285, 1161);
      tabPage1.TabIndex = 0;
      tabPage1.Text = "Preview";
      tabPage1.UseVisualStyleBackColor = true;
      // 
      // showLineSeparatorsCheckBox
      // 
      showLineSeparatorsCheckBox.AutoSize = true;
      showLineSeparatorsCheckBox.Location = new Point(560, 411);
      showLineSeparatorsCheckBox.Name = "showLineSeparatorsCheckBox";
      showLineSeparatorsCheckBox.Size = new Size(208, 29);
      showLineSeparatorsCheckBox.TabIndex = 30;
      showLineSeparatorsCheckBox.Text = "Show Line Separators";
      showLineSeparatorsCheckBox.UseVisualStyleBackColor = true;
      showLineSeparatorsCheckBox.CheckedChanged += showLineSeparatorsCheckBox_CheckedChanged;
      // 
      // PreviewTextBox
      // 
      PreviewTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left;
      PreviewTextBox.Font = new Font("Segoe UI", 12F, FontStyle.Regular, GraphicsUnit.Point, 0);
      PreviewTextBox.Location = new Point(15, 48);
      PreviewTextBox.Multiline = true;
      PreviewTextBox.Name = "PreviewTextBox";
      PreviewTextBox.Size = new Size(536, 392);
      PreviewTextBox.TabIndex = 19;
      PreviewTextBox.Text = "0123456789+-=/<>\r\n(){}[]_/?<>,.!|@#$%^&*~\r\nABCDEFGHIJKLMNOPQRSTUVWXYZ\r\nabcdefghijklmnopqrstuvwxyz\r\n";
      PreviewTextBox.TextChanged += PreviewTextBox_TextChanged;
      // 
      // groupBox9
      // 
      groupBox9.Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      groupBox9.Controls.Add(VLWCharPanel);
      groupBox9.Controls.Add(groupBox5);
      groupBox9.Controls.Add(groupBox6);
      groupBox9.Location = new Point(999, 446);
      groupBox9.Name = "groupBox9";
      groupBox9.Size = new Size(1277, 669);
      groupBox9.TabIndex = 29;
      groupBox9.TabStop = false;
      groupBox9.Text = "Generated VLW";
      // 
      // VLWCharPanel
      // 
      VLWCharPanel.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      VLWCharPanel.BackColor = Color.DimGray;
      VLWCharPanel.Location = new Point(6, 30);
      VLWCharPanel.Name = "VLWCharPanel";
      VLWCharPanel.Size = new Size(595, 633);
      VLWCharPanel.TabIndex = 2;
      // 
      // groupBox5
      // 
      groupBox5.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      groupBox5.Controls.Add(vlwAscentTextBox);
      groupBox5.Controls.Add(vlwDescentTextBox);
      groupBox5.Controls.Add(label15);
      groupBox5.Controls.Add(label16);
      groupBox5.Location = new Point(607, 30);
      groupBox5.Name = "groupBox5";
      groupBox5.Size = new Size(263, 116);
      groupBox5.TabIndex = 22;
      groupBox5.TabStop = false;
      groupBox5.Text = "Font";
      // 
      // vlwAscentTextBox
      // 
      vlwAscentTextBox.Location = new Point(119, 30);
      vlwAscentTextBox.Name = "vlwAscentTextBox";
      vlwAscentTextBox.ReadOnly = true;
      vlwAscentTextBox.Size = new Size(108, 31);
      vlwAscentTextBox.TabIndex = 13;
      // 
      // vlwDescentTextBox
      // 
      vlwDescentTextBox.Location = new Point(119, 67);
      vlwDescentTextBox.Name = "vlwDescentTextBox";
      vlwDescentTextBox.ReadOnly = true;
      vlwDescentTextBox.Size = new Size(108, 31);
      vlwDescentTextBox.TabIndex = 13;
      // 
      // label15
      // 
      label15.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label15.AutoSize = true;
      label15.Location = new Point(44, 33);
      label15.Name = "label15";
      label15.Size = new Size(69, 25);
      label15.TabIndex = 10;
      label15.Text = "Ascent:";
      // 
      // label16
      // 
      label16.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label16.AutoSize = true;
      label16.Location = new Point(34, 70);
      label16.Name = "label16";
      label16.Size = new Size(79, 25);
      label16.TabIndex = 10;
      label16.Text = "Descent:";
      // 
      // groupBox6
      // 
      groupBox6.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      groupBox6.Controls.Add(label19);
      groupBox6.Controls.Add(vlwWidthTextBox);
      groupBox6.Controls.Add(label22);
      groupBox6.Controls.Add(vlwHeightTextBox);
      groupBox6.Controls.Add(label21);
      groupBox6.Controls.Add(gdXTextBox);
      groupBox6.Controls.Add(label20);
      groupBox6.Controls.Add(gdYTextBox);
      groupBox6.Controls.Add(gxAdvanceTextBox);
      groupBox6.Controls.Add(label18);
      groupBox6.Controls.Add(paddingTextBox);
      groupBox6.Controls.Add(label17);
      groupBox6.Location = new Point(607, 152);
      groupBox6.Name = "groupBox6";
      groupBox6.Size = new Size(263, 275);
      groupBox6.TabIndex = 23;
      groupBox6.TabStop = false;
      groupBox6.Text = "Glyph";
      // 
      // label19
      // 
      label19.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label19.AutoSize = true;
      label19.Location = new Point(59, 44);
      label19.Name = "label19";
      label19.Size = new Size(64, 25);
      label19.TabIndex = 21;
      label19.Text = "Width:";
      // 
      // vlwWidthTextBox
      // 
      vlwWidthTextBox.Location = new Point(129, 41);
      vlwWidthTextBox.Name = "vlwWidthTextBox";
      vlwWidthTextBox.ReadOnly = true;
      vlwWidthTextBox.Size = new Size(108, 31);
      vlwWidthTextBox.TabIndex = 13;
      // 
      // label22
      // 
      label22.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label22.AutoSize = true;
      label22.Location = new Point(44, 229);
      label22.Name = "label22";
      label22.Size = new Size(81, 25);
      label22.TabIndex = 21;
      label22.Text = "Padding:";
      // 
      // vlwHeightTextBox
      // 
      vlwHeightTextBox.Location = new Point(129, 78);
      vlwHeightTextBox.Name = "vlwHeightTextBox";
      vlwHeightTextBox.ReadOnly = true;
      vlwHeightTextBox.Size = new Size(108, 31);
      vlwHeightTextBox.TabIndex = 13;
      // 
      // label21
      // 
      label21.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label21.AutoSize = true;
      label21.Location = new Point(21, 192);
      label21.Name = "label21";
      label21.Size = new Size(103, 25);
      label21.TabIndex = 21;
      label21.Text = "gxAdvance:";
      // 
      // gdXTextBox
      // 
      gdXTextBox.Location = new Point(129, 115);
      gdXTextBox.Name = "gdXTextBox";
      gdXTextBox.ReadOnly = true;
      gdXTextBox.Size = new Size(108, 31);
      gdXTextBox.TabIndex = 13;
      // 
      // label20
      // 
      label20.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label20.AutoSize = true;
      label20.Location = new Point(54, 81);
      label20.Name = "label20";
      label20.Size = new Size(69, 25);
      label20.TabIndex = 21;
      label20.Text = "Height:";
      // 
      // gdYTextBox
      // 
      gdYTextBox.Location = new Point(129, 152);
      gdYTextBox.Name = "gdYTextBox";
      gdYTextBox.ReadOnly = true;
      gdYTextBox.Size = new Size(108, 31);
      gdYTextBox.TabIndex = 13;
      // 
      // gxAdvanceTextBox
      // 
      gxAdvanceTextBox.Location = new Point(129, 189);
      gxAdvanceTextBox.Name = "gxAdvanceTextBox";
      gxAdvanceTextBox.ReadOnly = true;
      gxAdvanceTextBox.Size = new Size(108, 31);
      gxAdvanceTextBox.TabIndex = 13;
      // 
      // label18
      // 
      label18.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label18.AutoSize = true;
      label18.Location = new Point(75, 155);
      label18.Name = "label18";
      label18.Size = new Size(48, 25);
      label18.TabIndex = 21;
      label18.Text = "gdY:";
      // 
      // paddingTextBox
      // 
      paddingTextBox.Location = new Point(129, 226);
      paddingTextBox.Name = "paddingTextBox";
      paddingTextBox.ReadOnly = true;
      paddingTextBox.Size = new Size(108, 31);
      paddingTextBox.TabIndex = 13;
      // 
      // label17
      // 
      label17.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label17.AutoSize = true;
      label17.Location = new Point(75, 118);
      label17.Name = "label17";
      label17.Size = new Size(49, 25);
      label17.TabIndex = 10;
      label17.Text = "gdX:";
      // 
      // groupBox8
      // 
      groupBox8.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      groupBox8.Controls.Add(groupBox2);
      groupBox8.Controls.Add(groupBox7);
      groupBox8.Controls.Add(TrueTypeCharPanel);
      groupBox8.Location = new Point(9, 446);
      groupBox8.Name = "groupBox8";
      groupBox8.Size = new Size(984, 669);
      groupBox8.TabIndex = 28;
      groupBox8.TabStop = false;
      groupBox8.Text = "TrueType";
      // 
      // groupBox2
      // 
      groupBox2.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      groupBox2.Controls.Add(label25);
      groupBox2.Controls.Add(ttDescentLabel);
      groupBox2.Controls.Add(label26);
      groupBox2.Controls.Add(label1);
      groupBox2.Controls.Add(ttAscentLabel);
      groupBox2.Controls.Add(ttLineSpacingTextBox);
      groupBox2.Controls.Add(ttDescentTextBox);
      groupBox2.Controls.Add(ttSizeTextBox);
      groupBox2.Controls.Add(ttHeightTextBox);
      groupBox2.Controls.Add(ttAscentTextBox);
      groupBox2.Location = new Point(714, 30);
      groupBox2.Name = "groupBox2";
      groupBox2.Size = new Size(263, 248);
      groupBox2.TabIndex = 23;
      groupBox2.TabStop = false;
      groupBox2.Text = "What GDI Reports";
      // 
      // label25
      // 
      label25.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label25.AutoSize = true;
      label25.Location = new Point(4, 188);
      label25.Name = "label25";
      label25.Size = new Size(115, 25);
      label25.TabIndex = 21;
      label25.Text = "Line Spacing:";
      label25.TextAlign = ContentAlignment.TopRight;
      // 
      // ttDescentLabel
      // 
      ttDescentLabel.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      ttDescentLabel.AutoSize = true;
      ttDescentLabel.Location = new Point(40, 151);
      ttDescentLabel.Name = "ttDescentLabel";
      ttDescentLabel.Size = new Size(79, 25);
      ttDescentLabel.TabIndex = 21;
      ttDescentLabel.Text = "Descent:";
      ttDescentLabel.TextAlign = ContentAlignment.TopRight;
      // 
      // label26
      // 
      label26.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label26.AutoSize = true;
      label26.Location = new Point(72, 77);
      label26.Name = "label26";
      label26.Size = new Size(47, 25);
      label26.TabIndex = 21;
      label26.Text = "Size:";
      label26.TextAlign = ContentAlignment.TopRight;
      // 
      // label1
      // 
      label1.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label1.AutoSize = true;
      label1.Location = new Point(53, 40);
      label1.Name = "label1";
      label1.Size = new Size(69, 25);
      label1.TabIndex = 21;
      label1.Text = "Height:";
      label1.TextAlign = ContentAlignment.TopRight;
      // 
      // ttAscentLabel
      // 
      ttAscentLabel.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      ttAscentLabel.AutoSize = true;
      ttAscentLabel.Location = new Point(50, 114);
      ttAscentLabel.Name = "ttAscentLabel";
      ttAscentLabel.Size = new Size(69, 25);
      ttAscentLabel.TabIndex = 21;
      ttAscentLabel.Text = "Ascent:";
      ttAscentLabel.TextAlign = ContentAlignment.TopRight;
      // 
      // ttLineSpacingTextBox
      // 
      ttLineSpacingTextBox.Location = new Point(125, 185);
      ttLineSpacingTextBox.Name = "ttLineSpacingTextBox";
      ttLineSpacingTextBox.ReadOnly = true;
      ttLineSpacingTextBox.Size = new Size(108, 31);
      ttLineSpacingTextBox.TabIndex = 13;
      // 
      // ttDescentTextBox
      // 
      ttDescentTextBox.Location = new Point(125, 148);
      ttDescentTextBox.Name = "ttDescentTextBox";
      ttDescentTextBox.ReadOnly = true;
      ttDescentTextBox.Size = new Size(108, 31);
      ttDescentTextBox.TabIndex = 13;
      // 
      // ttSizeTextBox
      // 
      ttSizeTextBox.Location = new Point(128, 74);
      ttSizeTextBox.Name = "ttSizeTextBox";
      ttSizeTextBox.ReadOnly = true;
      ttSizeTextBox.Size = new Size(108, 31);
      ttSizeTextBox.TabIndex = 13;
      // 
      // ttHeightTextBox
      // 
      ttHeightTextBox.Location = new Point(128, 37);
      ttHeightTextBox.Name = "ttHeightTextBox";
      ttHeightTextBox.ReadOnly = true;
      ttHeightTextBox.Size = new Size(108, 31);
      ttHeightTextBox.TabIndex = 13;
      // 
      // ttAscentTextBox
      // 
      ttAscentTextBox.Location = new Point(125, 111);
      ttAscentTextBox.Name = "ttAscentTextBox";
      ttAscentTextBox.ReadOnly = true;
      ttAscentTextBox.Size = new Size(108, 31);
      ttAscentTextBox.TabIndex = 13;
      // 
      // groupBox7
      // 
      groupBox7.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      groupBox7.Controls.Add(label24);
      groupBox7.Controls.Add(label2);
      groupBox7.Controls.Add(label23);
      groupBox7.Controls.Add(label31);
      groupBox7.Controls.Add(ttCharWidthTextBox);
      groupBox7.Controls.Add(ttCharHeightTextBox);
      groupBox7.Controls.Add(ttCellWidthTextBox);
      groupBox7.Controls.Add(label9);
      groupBox7.Controls.Add(ttCellHeightTextBox);
      groupBox7.Controls.Add(label37);
      groupBox7.Location = new Point(714, 284);
      groupBox7.Name = "groupBox7";
      groupBox7.Size = new Size(263, 322);
      groupBox7.TabIndex = 23;
      groupBox7.TabStop = false;
      groupBox7.Text = "What We Measure";
      // 
      // label24
      // 
      label24.AutoSize = true;
      label24.Font = new Font("Segoe UI", 9F, FontStyle.Bold, GraphicsUnit.Point, 0);
      label24.Location = new Point(31, 193);
      label24.Name = "label24";
      label24.Size = new Size(51, 25);
      label24.TabIndex = 21;
      label24.Text = "Char";
      label24.TextAlign = ContentAlignment.TopRight;
      // 
      // label2
      // 
      label2.AutoSize = true;
      label2.Font = new Font("Segoe UI", 9F, FontStyle.Bold, GraphicsUnit.Point, 0);
      label2.Location = new Point(39, 44);
      label2.Name = "label2";
      label2.Size = new Size(43, 25);
      label2.TabIndex = 21;
      label2.Text = "Cell";
      label2.TextAlign = ContentAlignment.TopRight;
      // 
      // label23
      // 
      label23.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label23.AutoSize = true;
      label23.Location = new Point(55, 218);
      label23.Name = "label23";
      label23.Size = new Size(64, 25);
      label23.TabIndex = 21;
      label23.Text = "Width:";
      label23.TextAlign = ContentAlignment.TopRight;
      // 
      // label31
      // 
      label31.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label31.AutoSize = true;
      label31.Location = new Point(55, 82);
      label31.Name = "label31";
      label31.Size = new Size(64, 25);
      label31.TabIndex = 21;
      label31.Text = "Width:";
      label31.TextAlign = ContentAlignment.TopRight;
      // 
      // ttCharWidthTextBox
      // 
      ttCharWidthTextBox.Location = new Point(125, 215);
      ttCharWidthTextBox.Name = "ttCharWidthTextBox";
      ttCharWidthTextBox.ReadOnly = true;
      ttCharWidthTextBox.Size = new Size(108, 31);
      ttCharWidthTextBox.TabIndex = 13;
      // 
      // ttCharHeightTextBox
      // 
      ttCharHeightTextBox.Location = new Point(125, 252);
      ttCharHeightTextBox.Name = "ttCharHeightTextBox";
      ttCharHeightTextBox.ReadOnly = true;
      ttCharHeightTextBox.Size = new Size(108, 31);
      ttCharHeightTextBox.TabIndex = 13;
      // 
      // ttCellWidthTextBox
      // 
      ttCellWidthTextBox.Location = new Point(125, 79);
      ttCellWidthTextBox.Name = "ttCellWidthTextBox";
      ttCellWidthTextBox.ReadOnly = true;
      ttCellWidthTextBox.Size = new Size(108, 31);
      ttCellWidthTextBox.TabIndex = 13;
      // 
      // label9
      // 
      label9.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label9.AutoSize = true;
      label9.Location = new Point(50, 255);
      label9.Name = "label9";
      label9.Size = new Size(69, 25);
      label9.TabIndex = 21;
      label9.Text = "Height:";
      label9.TextAlign = ContentAlignment.TopRight;
      // 
      // ttCellHeightTextBox
      // 
      ttCellHeightTextBox.Location = new Point(125, 116);
      ttCellHeightTextBox.Name = "ttCellHeightTextBox";
      ttCellHeightTextBox.ReadOnly = true;
      ttCellHeightTextBox.Size = new Size(108, 31);
      ttCellHeightTextBox.TabIndex = 13;
      // 
      // label37
      // 
      label37.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label37.AutoSize = true;
      label37.Location = new Point(50, 119);
      label37.Name = "label37";
      label37.Size = new Size(69, 25);
      label37.TabIndex = 21;
      label37.Text = "Height:";
      label37.TextAlign = ContentAlignment.TopRight;
      // 
      // TrueTypeCharPanel
      // 
      TrueTypeCharPanel.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      TrueTypeCharPanel.BackColor = Color.DimGray;
      TrueTypeCharPanel.Location = new Point(6, 30);
      TrueTypeCharPanel.Name = "TrueTypeCharPanel";
      TrueTypeCharPanel.Size = new Size(702, 633);
      TrueTypeCharPanel.TabIndex = 1;
      // 
      // label29
      // 
      label29.AutoSize = true;
      label29.Location = new Point(15, 20);
      label29.Name = "label29";
      label29.Size = new Size(107, 25);
      label29.TabIndex = 10;
      label29.Text = "Preview Text";
      // 
      // label28
      // 
      label28.AutoSize = true;
      label28.Location = new Point(1347, 20);
      label28.Name = "label28";
      label28.Size = new Size(47, 25);
      label28.TabIndex = 10;
      label28.Text = "VLW";
      // 
      // label27
      // 
      label27.AutoSize = true;
      label27.Location = new Point(557, 20);
      label27.Name = "label27";
      label27.Size = new Size(81, 25);
      label27.TabIndex = 10;
      label27.Text = "TrueType";
      // 
      // TrueTypeTextPanel
      // 
      TrueTypeTextPanel.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left;
      TrueTypeTextPanel.BackColor = Color.Black;
      TrueTypeTextPanel.Location = new Point(557, 48);
      TrueTypeTextPanel.Name = "TrueTypeTextPanel";
      TrueTypeTextPanel.Size = new Size(784, 357);
      TrueTypeTextPanel.TabIndex = 20;
      TrueTypeTextPanel.Paint += TrueTypePreviewPanel_Paint;
      // 
      // VLWTextPanel
      // 
      VLWTextPanel.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      VLWTextPanel.BackColor = Color.Black;
      VLWTextPanel.Location = new Point(1347, 48);
      VLWTextPanel.Name = "VLWTextPanel";
      VLWTextPanel.Size = new Size(929, 357);
      VLWTextPanel.TabIndex = 20;
      VLWTextPanel.Paint += VLWPreviewPanel_Paint;
      // 
      // HexLabel
      // 
      HexLabel.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      HexLabel.AutoSize = true;
      HexLabel.Location = new Point(176, 1124);
      HexLabel.Name = "HexLabel";
      HexLabel.Size = new Size(50, 25);
      HexLabel.TabIndex = 18;
      HexLabel.Text = "0x00";
      // 
      // label5
      // 
      label5.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      label5.AutoSize = true;
      label5.Location = new Point(9, 1124);
      label5.Name = "label5";
      label5.Size = new Size(117, 25);
      label5.TabIndex = 6;
      label5.Text = "Preview Char:";
      // 
      // TestCharTextBox
      // 
      TestCharTextBox.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      TestCharTextBox.Location = new Point(132, 1121);
      TestCharTextBox.Name = "TestCharTextBox";
      TestCharTextBox.Size = new Size(38, 31);
      TestCharTextBox.TabIndex = 5;
      TestCharTextBox.Text = "0";
      TestCharTextBox.TextChanged += TestCharTextBox_TextChanged;
      // 
      // tabPage2
      // 
      tabPage2.Controls.Add(panel1);
      tabPage2.Location = new Point(4, 34);
      tabPage2.Name = "tabPage2";
      tabPage2.Padding = new Padding(6);
      tabPage2.Size = new Size(2285, 1161);
      tabPage2.TabIndex = 1;
      tabPage2.Text = "Full Char";
      tabPage2.UseVisualStyleBackColor = true;
      // 
      // panel1
      // 
      panel1.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      panel1.BackColor = Color.DimGray;
      panel1.Location = new Point(9, 9);
      panel1.Name = "panel1";
      panel1.Size = new Size(1597, 1143);
      panel1.TabIndex = 0;
      panel1.Paint += panel1_Paint;
      // 
      // groupBox4
      // 
      groupBox4.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      groupBox4.Controls.Add(label14);
      groupBox4.Controls.Add(digitCheckBox);
      groupBox4.Controls.Add(label32);
      groupBox4.Controls.Add(label6);
      groupBox4.Controls.Add(label4);
      groupBox4.Controls.Add(label30);
      groupBox4.Controls.Add(label3);
      groupBox4.Controls.Add(verticalPaddingUpDown);
      groupBox4.Controls.Add(horizontalPaddingUpDown);
      groupBox4.Controls.Add(label7);
      groupBox4.Controls.Add(cellHeightUpDown);
      groupBox4.Controls.Add(label8);
      groupBox4.Controls.Add(monospaceCheckBox);
      groupBox4.Location = new Point(2311, 189);
      groupBox4.Name = "groupBox4";
      groupBox4.Size = new Size(334, 372);
      groupBox4.TabIndex = 27;
      groupBox4.TabStop = false;
      groupBox4.Text = "Options";
      // 
      // label14
      // 
      label14.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label14.AutoSize = true;
      label14.Location = new Point(131, 87);
      label14.Name = "label14";
      label14.Size = new Size(54, 25);
      label14.TabIndex = 22;
      label14.Text = "Digit:";
      // 
      // digitCheckBox
      // 
      digitCheckBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      digitCheckBox.AutoSize = true;
      digitCheckBox.Location = new Point(191, 90);
      digitCheckBox.Name = "digitCheckBox";
      digitCheckBox.Size = new Size(22, 21);
      digitCheckBox.TabIndex = 21;
      digitCheckBox.UseVisualStyleBackColor = true;
      digitCheckBox.CheckedChanged += digitCheckBox_CheckedChanged;
      // 
      // label32
      // 
      label32.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label32.AutoSize = true;
      label32.Location = new Point(291, 156);
      label32.Name = "label32";
      label32.Size = new Size(31, 25);
      label32.TabIndex = 7;
      label32.Text = "px";
      // 
      // label6
      // 
      label6.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label6.AutoSize = true;
      label6.Location = new Point(291, 119);
      label6.Name = "label6";
      label6.Size = new Size(31, 25);
      label6.TabIndex = 7;
      label6.Text = "px";
      // 
      // label4
      // 
      label4.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label4.AutoSize = true;
      label4.Location = new Point(294, 27);
      label4.Name = "label4";
      label4.Size = new Size(31, 25);
      label4.TabIndex = 7;
      label4.Text = "px";
      // 
      // label30
      // 
      label30.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label30.AutoSize = true;
      label30.Location = new Point(43, 156);
      label30.Name = "label30";
      label30.Size = new Size(142, 25);
      label30.TabIndex = 6;
      label30.Text = "Vertical Padding:";
      // 
      // label3
      // 
      label3.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label3.AutoSize = true;
      label3.Location = new Point(17, 119);
      label3.Name = "label3";
      label3.Size = new Size(168, 25);
      label3.TabIndex = 6;
      label3.Text = "Horizontal Padding:";
      // 
      // verticalPaddingUpDown
      // 
      verticalPaddingUpDown.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      verticalPaddingUpDown.Location = new Point(191, 154);
      verticalPaddingUpDown.Name = "verticalPaddingUpDown";
      verticalPaddingUpDown.Size = new Size(94, 31);
      verticalPaddingUpDown.TabIndex = 20;
      verticalPaddingUpDown.ValueChanged += SpacingUpDown_ValueChanged;
      // 
      // horizontalPaddingUpDown
      // 
      horizontalPaddingUpDown.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      horizontalPaddingUpDown.Location = new Point(191, 117);
      horizontalPaddingUpDown.Name = "horizontalPaddingUpDown";
      horizontalPaddingUpDown.Size = new Size(94, 31);
      horizontalPaddingUpDown.TabIndex = 20;
      horizontalPaddingUpDown.Value = new decimal(new int[] { 1, 0, 0, 0 });
      horizontalPaddingUpDown.ValueChanged += SpacingUpDown_ValueChanged;
      // 
      // label7
      // 
      label7.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label7.AutoSize = true;
      label7.Location = new Point(116, 27);
      label7.Name = "label7";
      label7.Size = new Size(69, 25);
      label7.TabIndex = 6;
      label7.Text = "Height:";
      // 
      // cellHeightUpDown
      // 
      cellHeightUpDown.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      cellHeightUpDown.Location = new Point(191, 25);
      cellHeightUpDown.Minimum = new decimal(new int[] { 8, 0, 0, 0 });
      cellHeightUpDown.Name = "cellHeightUpDown";
      cellHeightUpDown.Size = new Size(94, 31);
      cellHeightUpDown.TabIndex = 20;
      cellHeightUpDown.Value = new decimal(new int[] { 32, 0, 0, 0 });
      cellHeightUpDown.ValueChanged += charHeightUpDown_ValueChanged;
      // 
      // label8
      // 
      label8.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label8.AutoSize = true;
      label8.Location = new Point(76, 59);
      label8.Name = "label8";
      label8.Size = new Size(109, 25);
      label8.TabIndex = 16;
      label8.Text = "Monospace:";
      // 
      // monospaceCheckBox
      // 
      monospaceCheckBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      monospaceCheckBox.AutoSize = true;
      monospaceCheckBox.Location = new Point(191, 62);
      monospaceCheckBox.Name = "monospaceCheckBox";
      monospaceCheckBox.Size = new Size(22, 21);
      monospaceCheckBox.TabIndex = 15;
      monospaceCheckBox.UseVisualStyleBackColor = true;
      monospaceCheckBox.CheckedChanged += monospaceCheckBox_CheckedChanged;
      // 
      // groupBox3
      // 
      groupBox3.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      groupBox3.Controls.Add(label13);
      groupBox3.Controls.Add(label12);
      groupBox3.Controls.Add(label11);
      groupBox3.Controls.Add(boldCheckBox);
      groupBox3.Controls.Add(FontComboBox);
      groupBox3.Controls.Add(italicCheckBox);
      groupBox3.Location = new Point(2311, 46);
      groupBox3.Name = "groupBox3";
      groupBox3.Size = new Size(334, 137);
      groupBox3.TabIndex = 26;
      groupBox3.TabStop = false;
      groupBox3.Text = "True-Type Font";
      // 
      // label13
      // 
      label13.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label13.AutoSize = true;
      label13.Location = new Point(73, 94);
      label13.Name = "label13";
      label13.Size = new Size(52, 25);
      label13.TabIndex = 27;
      label13.Text = "Italic:";
      // 
      // label12
      // 
      label12.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label12.AutoSize = true;
      label12.Location = new Point(73, 69);
      label12.Name = "label12";
      label12.Size = new Size(52, 25);
      label12.TabIndex = 27;
      label12.Text = "Bold:";
      // 
      // label11
      // 
      label11.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label11.AutoSize = true;
      label11.Location = new Point(59, 33);
      label11.Name = "label11";
      label11.Size = new Size(66, 25);
      label11.TabIndex = 27;
      label11.Text = "Family:";
      // 
      // boldCheckBox
      // 
      boldCheckBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      boldCheckBox.AutoSize = true;
      boldCheckBox.Location = new Point(131, 72);
      boldCheckBox.Name = "boldCheckBox";
      boldCheckBox.Size = new Size(22, 21);
      boldCheckBox.TabIndex = 25;
      boldCheckBox.UseVisualStyleBackColor = true;
      boldCheckBox.CheckedChanged += boldCheckBox_CheckedChanged;
      // 
      // FontComboBox
      // 
      FontComboBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      FontComboBox.FormattingEnabled = true;
      FontComboBox.Location = new Point(131, 30);
      FontComboBox.Name = "FontComboBox";
      FontComboBox.Size = new Size(197, 33);
      FontComboBox.TabIndex = 3;
      // 
      // italicCheckBox
      // 
      italicCheckBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      italicCheckBox.AutoSize = true;
      italicCheckBox.Location = new Point(131, 99);
      italicCheckBox.Name = "italicCheckBox";
      italicCheckBox.Size = new Size(22, 21);
      italicCheckBox.TabIndex = 24;
      italicCheckBox.UseVisualStyleBackColor = true;
      italicCheckBox.CheckedChanged += italicCheckBox_CheckedChanged;
      // 
      // SaveButton
      // 
      SaveButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      SaveButton.Location = new Point(2533, 1177);
      SaveButton.Name = "SaveButton";
      SaveButton.Size = new Size(112, 34);
      SaveButton.TabIndex = 9;
      SaveButton.Text = "Download";
      SaveButton.UseVisualStyleBackColor = true;
      SaveButton.Click += SaveButton_Click;
      // 
      // label10
      // 
      label10.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      label10.AutoSize = true;
      label10.Location = new Point(2374, 1143);
      label10.Name = "label10";
      label10.Size = new Size(130, 25);
      label10.TabIndex = 21;
      label10.Text = "Font Sizes (px):";
      // 
      // fontSizesTextBox
      // 
      fontSizesTextBox.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      fontSizesTextBox.Location = new Point(2510, 1140);
      fontSizesTextBox.Name = "fontSizesTextBox";
      fontSizesTextBox.Size = new Size(135, 31);
      fontSizesTextBox.TabIndex = 22;
      fontSizesTextBox.Text = "8,16,24,32,40,48,56,64";
      // 
      // statusTextBox
      // 
      statusTextBox.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      statusTextBox.Location = new Point(2370, 861);
      statusTextBox.Multiline = true;
      statusTextBox.Name = "statusTextBox";
      statusTextBox.Size = new Size(275, 273);
      statusTextBox.TabIndex = 23;
      // 
      // testButton1
      // 
      testButton1.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      testButton1.Location = new Point(2370, 776);
      testButton1.Name = "testButton1";
      testButton1.Size = new Size(87, 79);
      testButton1.TabIndex = 25;
      testButton1.Text = "Observe";
      testButton1.UseVisualStyleBackColor = true;
      testButton1.Click += testObserveButton_Click;
      // 
      // testButton2
      // 
      testButton2.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      testButton2.Location = new Point(2463, 776);
      testButton2.Name = "testButton2";
      testButton2.Size = new Size(89, 79);
      testButton2.TabIndex = 25;
      testButton2.Text = "Glyph";
      testButton2.UseVisualStyleBackColor = true;
      testButton2.Click += testGlyphButton_Click;
      // 
      // testButton3
      // 
      testButton3.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      testButton3.Location = new Point(2558, 776);
      testButton3.Name = "testButton3";
      testButton3.Size = new Size(87, 79);
      testButton3.TabIndex = 25;
      testButton3.Text = "Font";
      testButton3.UseVisualStyleBackColor = true;
      testButton3.Click += testFontButton_Click;
      // 
      // button1
      // 
      button1.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      button1.Location = new Point(2382, 724);
      button1.Name = "button1";
      button1.Size = new Size(265, 46);
      button1.TabIndex = 25;
      button1.Text = "Load Roboto32.vlw";
      button1.UseVisualStyleBackColor = true;
      button1.Click += button1_Click;
      // 
      // label33
      // 
      label33.AutoSize = true;
      label33.Location = new Point(1153, 412);
      label33.Name = "label33";
      label33.Size = new Size(124, 25);
      label33.TabIndex = 31;
      label33.Text = "Magnification:";
      // 
      // magnificationLabel
      // 
      magnificationLabel.AutoSize = true;
      magnificationLabel.Location = new Point(1282, 412);
      magnificationLabel.Name = "magnificationLabel";
      magnificationLabel.Size = new Size(34, 25);
      magnificationLabel.TabIndex = 32;
      magnificationLabel.Text = "#X";
      // 
      // MainForm
      // 
      AutoScaleDimensions = new SizeF(10F, 25F);
      AutoScaleMode = AutoScaleMode.Font;
      ClientSize = new Size(2657, 1223);
      Controls.Add(groupBox4);
      Controls.Add(testButton3);
      Controls.Add(groupBox3);
      Controls.Add(testButton2);
      Controls.Add(button1);
      Controls.Add(testButton1);
      Controls.Add(statusTextBox);
      Controls.Add(fontSizesTextBox);
      Controls.Add(label10);
      Controls.Add(tabControl1);
      Controls.Add(SaveButton);
      Name = "MainForm";
      Text = "Smooth Font Creator for TFT_eSPI";
      tabControl1.ResumeLayout(false);
      tabPage1.ResumeLayout(false);
      tabPage1.PerformLayout();
      groupBox9.ResumeLayout(false);
      groupBox5.ResumeLayout(false);
      groupBox5.PerformLayout();
      groupBox6.ResumeLayout(false);
      groupBox6.PerformLayout();
      groupBox8.ResumeLayout(false);
      groupBox2.ResumeLayout(false);
      groupBox2.PerformLayout();
      groupBox7.ResumeLayout(false);
      groupBox7.PerformLayout();
      tabPage2.ResumeLayout(false);
      groupBox4.ResumeLayout(false);
      groupBox4.PerformLayout();
      ((System.ComponentModel.ISupportInitialize)verticalPaddingUpDown).EndInit();
      ((System.ComponentModel.ISupportInitialize)horizontalPaddingUpDown).EndInit();
      ((System.ComponentModel.ISupportInitialize)cellHeightUpDown).EndInit();
      groupBox3.ResumeLayout(false);
      groupBox3.PerformLayout();
      ResumeLayout(false);
      PerformLayout();
   }

   #endregion

   private TabControl tabControl1;
   private TabPage tabPage1;
   private TabPage tabPage2;
   private Panel TrueTypeCharPanel;
   private ComboBox FontComboBox;
   private TextBox TestCharTextBox;
   private Label label7;
   private Button SaveButton;
   private Label label4;
   private Label label5;
   private Panel VLWCharPanel;
   private Label label8;
   private CheckBox monospaceCheckBox;
   private Label HexLabel;
   private Panel panel1;
   private NumericUpDown cellHeightUpDown;
   private Label label10;
   private TextBox fontSizesTextBox;
   private TextBox statusTextBox;
   private CheckBox italicCheckBox;
   private Panel VLWTextPanel;
   private TextBox PreviewTextBox;
   private Button testButton1;
   private Button testButton2;
   private Button testButton3;
   private GroupBox groupBox3;
   private Label label13;
   private Label label12;
   private Label label11;
   private CheckBox boldCheckBox;
   private GroupBox groupBox4;
   private Label label14;
   private CheckBox digitCheckBox;
   private Label label15;
   private TextBox vlwAscentTextBox;
   private Label label17;
   private Label label16;
   private Label label21;
   private Label label20;
   private Label label19;
   private Label label18;
   private Label label22;
   private TextBox paddingTextBox;
   private TextBox gxAdvanceTextBox;
   private TextBox gdYTextBox;
   private TextBox gdXTextBox;
   private TextBox vlwHeightTextBox;
   private TextBox vlwWidthTextBox;
   private TextBox vlwDescentTextBox;
   private GroupBox groupBox6;
   private GroupBox groupBox5;
   private Button button1;
   private Label label6;
   private Label label2;
   private Label label3;
   private Label label1;
   private NumericUpDown horizontalPaddingUpDown;
   private NumericUpDown numericUpDown1;
   private Panel TrueTypeTextPanel;
   private GroupBox groupBox7;
   private Label label31;
   private TextBox ttCellWidthTextBox;
   private TextBox ttCellHeightTextBox;
   private Label label37;
   private GroupBox groupBox2;
   private Label ttDescentLabel;
   private Label ttAscentLabel;
   private TextBox ttDescentTextBox;
   private TextBox ttAscentTextBox;
   private TextBox ttHeightTextBox;
   private Label label24;
   private Label label23;
   private TextBox ttCharWidthTextBox;
   private TextBox ttCharHeightTextBox;
   private Label label9;
   private GroupBox groupBox8;
   private GroupBox groupBox9;
   private Label label25;
   private TextBox ttLineSpacingTextBox;
   private Label label28;
   private Label label27;
   private Label label29;
   private Label label26;
   private TextBox ttSizeTextBox;
   private Label label32;
   private Label label30;
   private NumericUpDown verticalPaddingUpDown;
   private CheckBox showLineSeparatorsCheckBox;
   private Label magnificationLabel;
   private Label label33;
}

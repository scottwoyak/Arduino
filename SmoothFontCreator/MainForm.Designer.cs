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
      groupBox6 = new GroupBox();
      label19 = new Label();
      widthTextBox = new TextBox();
      label22 = new Label();
      heightTextBox = new TextBox();
      label21 = new Label();
      gdXTextBox = new TextBox();
      label20 = new Label();
      gdYTextBox = new TextBox();
      gxAdvanceTextBox = new TextBox();
      label18 = new Label();
      paddingTextBox = new TextBox();
      label17 = new Label();
      groupBox5 = new GroupBox();
      ascentTextBox = new TextBox();
      descentTextBox = new TextBox();
      label15 = new Label();
      label16 = new Label();
      label9 = new Label();
      PreviewPanel = new Panel();
      magnificationUpDown = new NumericUpDown();
      PreviewTextBox = new TextBox();
      showGlyphsCheckBox = new CheckBox();
      HexLabel = new Label();
      GlyphCharPanel = new Panel();
      label5 = new Label();
      TestCharTextBox = new TextBox();
      TrueTypeCharPanel = new Panel();
      tabPage2 = new TabPage();
      panel1 = new Panel();
      charHeightUpDown = new NumericUpDown();
      label8 = new Label();
      monospaceCheckBox = new CheckBox();
      SaveButton = new Button();
      label4 = new Label();
      label7 = new Label();
      FontComboBox = new ComboBox();
      label10 = new Label();
      fontSizesTextBox = new TextBox();
      statusTextBox = new TextBox();
      italicCheckBox = new CheckBox();
      testButton1 = new Button();
      testButton2 = new Button();
      testButton3 = new Button();
      groupBox3 = new GroupBox();
      label13 = new Label();
      label12 = new Label();
      label11 = new Label();
      boldCheckBox = new CheckBox();
      groupBox4 = new GroupBox();
      label14 = new Label();
      digitCheckBox = new CheckBox();
      label6 = new Label();
      label3 = new Label();
      SpacingUpDown = new NumericUpDown();
      button1 = new Button();
      tabControl1.SuspendLayout();
      tabPage1.SuspendLayout();
      groupBox6.SuspendLayout();
      groupBox5.SuspendLayout();
      ((System.ComponentModel.ISupportInitialize)magnificationUpDown).BeginInit();
      tabPage2.SuspendLayout();
      ((System.ComponentModel.ISupportInitialize)charHeightUpDown).BeginInit();
      groupBox3.SuspendLayout();
      groupBox4.SuspendLayout();
      ((System.ComponentModel.ISupportInitialize)SpacingUpDown).BeginInit();
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
      tabControl1.Size = new Size(2116, 1199);
      tabControl1.TabIndex = 0;
      // 
      // tabPage1
      // 
      tabPage1.Controls.Add(groupBox6);
      tabPage1.Controls.Add(groupBox5);
      tabPage1.Controls.Add(label9);
      tabPage1.Controls.Add(PreviewPanel);
      tabPage1.Controls.Add(magnificationUpDown);
      tabPage1.Controls.Add(PreviewTextBox);
      tabPage1.Controls.Add(showGlyphsCheckBox);
      tabPage1.Controls.Add(HexLabel);
      tabPage1.Controls.Add(GlyphCharPanel);
      tabPage1.Controls.Add(label5);
      tabPage1.Controls.Add(TestCharTextBox);
      tabPage1.Controls.Add(TrueTypeCharPanel);
      tabPage1.Location = new Point(4, 34);
      tabPage1.Name = "tabPage1";
      tabPage1.Padding = new Padding(6);
      tabPage1.Size = new Size(2108, 1161);
      tabPage1.TabIndex = 0;
      tabPage1.Text = "Preview";
      tabPage1.UseVisualStyleBackColor = true;
      // 
      // groupBox6
      // 
      groupBox6.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      groupBox6.Controls.Add(label19);
      groupBox6.Controls.Add(widthTextBox);
      groupBox6.Controls.Add(label22);
      groupBox6.Controls.Add(heightTextBox);
      groupBox6.Controls.Add(label21);
      groupBox6.Controls.Add(gdXTextBox);
      groupBox6.Controls.Add(label20);
      groupBox6.Controls.Add(gdYTextBox);
      groupBox6.Controls.Add(gxAdvanceTextBox);
      groupBox6.Controls.Add(label18);
      groupBox6.Controls.Add(paddingTextBox);
      groupBox6.Controls.Add(label17);
      groupBox6.Location = new Point(1342, 601);
      groupBox6.Name = "groupBox6";
      groupBox6.Size = new Size(263, 288);
      groupBox6.TabIndex = 23;
      groupBox6.TabStop = false;
      groupBox6.Text = "VLF Glyph";
      // 
      // label19
      // 
      label19.AutoSize = true;
      label19.Location = new Point(59, 44);
      label19.Name = "label19";
      label19.Size = new Size(64, 25);
      label19.TabIndex = 21;
      label19.Text = "Width:";
      // 
      // widthTextBox
      // 
      widthTextBox.Location = new Point(129, 41);
      widthTextBox.Name = "widthTextBox";
      widthTextBox.ReadOnly = true;
      widthTextBox.Size = new Size(108, 31);
      widthTextBox.TabIndex = 13;
      // 
      // label22
      // 
      label22.AutoSize = true;
      label22.Location = new Point(44, 229);
      label22.Name = "label22";
      label22.Size = new Size(81, 25);
      label22.TabIndex = 21;
      label22.Text = "Padding:";
      // 
      // heightTextBox
      // 
      heightTextBox.Location = new Point(129, 78);
      heightTextBox.Name = "heightTextBox";
      heightTextBox.ReadOnly = true;
      heightTextBox.Size = new Size(108, 31);
      heightTextBox.TabIndex = 13;
      // 
      // label21
      // 
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
      label17.AutoSize = true;
      label17.Location = new Point(75, 118);
      label17.Name = "label17";
      label17.Size = new Size(49, 25);
      label17.TabIndex = 10;
      label17.Text = "gdX:";
      // 
      // groupBox5
      // 
      groupBox5.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      groupBox5.Controls.Add(ascentTextBox);
      groupBox5.Controls.Add(descentTextBox);
      groupBox5.Controls.Add(label15);
      groupBox5.Controls.Add(label16);
      groupBox5.Location = new Point(1342, 445);
      groupBox5.Name = "groupBox5";
      groupBox5.Size = new Size(263, 150);
      groupBox5.TabIndex = 22;
      groupBox5.TabStop = false;
      groupBox5.Text = "VLF Font";
      // 
      // ascentTextBox
      // 
      ascentTextBox.Location = new Point(119, 30);
      ascentTextBox.Name = "ascentTextBox";
      ascentTextBox.ReadOnly = true;
      ascentTextBox.Size = new Size(108, 31);
      ascentTextBox.TabIndex = 13;
      // 
      // descentTextBox
      // 
      descentTextBox.Location = new Point(119, 67);
      descentTextBox.Name = "descentTextBox";
      descentTextBox.ReadOnly = true;
      descentTextBox.Size = new Size(108, 31);
      descentTextBox.TabIndex = 13;
      // 
      // label15
      // 
      label15.AutoSize = true;
      label15.Location = new Point(44, 33);
      label15.Name = "label15";
      label15.Size = new Size(69, 25);
      label15.TabIndex = 10;
      label15.Text = "Ascent:";
      // 
      // label16
      // 
      label16.AutoSize = true;
      label16.Location = new Point(34, 70);
      label16.Name = "label16";
      label16.Size = new Size(79, 25);
      label16.TabIndex = 10;
      label16.Text = "Descent:";
      // 
      // label9
      // 
      label9.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      label9.AutoSize = true;
      label9.Location = new Point(825, 411);
      label9.Name = "label9";
      label9.Size = new Size(120, 25);
      label9.TabIndex = 6;
      label9.Text = "Magnification";
      // 
      // PreviewPanel
      // 
      PreviewPanel.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      PreviewPanel.BackColor = Color.Black;
      PreviewPanel.Location = new Point(616, 9);
      PreviewPanel.Name = "PreviewPanel";
      PreviewPanel.Size = new Size(1483, 394);
      PreviewPanel.TabIndex = 20;
      PreviewPanel.Paint += PreviewPanel_Paint;
      // 
      // magnificationUpDown
      // 
      magnificationUpDown.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      magnificationUpDown.Location = new Point(951, 409);
      magnificationUpDown.Minimum = new decimal(new int[] { 1, 0, 0, 0 });
      magnificationUpDown.Name = "magnificationUpDown";
      magnificationUpDown.Size = new Size(68, 31);
      magnificationUpDown.TabIndex = 5;
      magnificationUpDown.Value = new decimal(new int[] { 2, 0, 0, 0 });
      magnificationUpDown.ValueChanged += magnificationUpDown_ValueChanged;
      // 
      // PreviewTextBox
      // 
      PreviewTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left;
      PreviewTextBox.BackColor = Color.Black;
      PreviewTextBox.Font = new Font("Segoe UI", 12F, FontStyle.Regular, GraphicsUnit.Point, 0);
      PreviewTextBox.ForeColor = SystemColors.Window;
      PreviewTextBox.Location = new Point(9, 9);
      PreviewTextBox.Multiline = true;
      PreviewTextBox.Name = "PreviewTextBox";
      PreviewTextBox.Size = new Size(601, 430);
      PreviewTextBox.TabIndex = 19;
      PreviewTextBox.Text = "$@0123456789\r\nABCDEFGHIJKLMNOPQRSTUVWXYZ\r\nabcdefghijklmnopqrstuvwxyz";
      PreviewTextBox.TextChanged += PreviewTextBox_TextChanged;
      // 
      // showGlyphsCheckBox
      // 
      showGlyphsCheckBox.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      showGlyphsCheckBox.AutoSize = true;
      showGlyphsCheckBox.Checked = true;
      showGlyphsCheckBox.CheckState = CheckState.Checked;
      showGlyphsCheckBox.Location = new Point(616, 410);
      showGlyphsCheckBox.Name = "showGlyphsCheckBox";
      showGlyphsCheckBox.Size = new Size(141, 29);
      showGlyphsCheckBox.TabIndex = 4;
      showGlyphsCheckBox.Text = "Show Glyphs";
      showGlyphsCheckBox.UseVisualStyleBackColor = true;
      showGlyphsCheckBox.CheckedChanged += showGlyphsCheckBox_CheckedChanged;
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
      // GlyphCharPanel
      // 
      GlyphCharPanel.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      GlyphCharPanel.BackColor = Color.DimGray;
      GlyphCharPanel.Location = new Point(616, 445);
      GlyphCharPanel.Name = "GlyphCharPanel";
      GlyphCharPanel.Size = new Size(720, 670);
      GlyphCharPanel.TabIndex = 2;
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
      // TrueTypeCharPanel
      // 
      TrueTypeCharPanel.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      TrueTypeCharPanel.BackColor = Color.DimGray;
      TrueTypeCharPanel.Location = new Point(9, 445);
      TrueTypeCharPanel.Name = "TrueTypeCharPanel";
      TrueTypeCharPanel.Size = new Size(601, 670);
      TrueTypeCharPanel.TabIndex = 1;
      // 
      // tabPage2
      // 
      tabPage2.Controls.Add(panel1);
      tabPage2.Location = new Point(4, 34);
      tabPage2.Name = "tabPage2";
      tabPage2.Padding = new Padding(6);
      tabPage2.Size = new Size(2108, 1161);
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
      panel1.Size = new Size(2090, 1143);
      panel1.TabIndex = 0;
      panel1.Paint += panel1_Paint;
      // 
      // charHeightUpDown
      // 
      charHeightUpDown.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      charHeightUpDown.Location = new Point(131, 25);
      charHeightUpDown.Minimum = new decimal(new int[] { 8, 0, 0, 0 });
      charHeightUpDown.Name = "charHeightUpDown";
      charHeightUpDown.Size = new Size(94, 31);
      charHeightUpDown.TabIndex = 20;
      charHeightUpDown.Value = new decimal(new int[] { 32, 0, 0, 0 });
      charHeightUpDown.ValueChanged += charHeightUpDown_ValueChanged;
      // 
      // label8
      // 
      label8.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label8.AutoSize = true;
      label8.Location = new Point(16, 59);
      label8.Name = "label8";
      label8.Size = new Size(109, 25);
      label8.TabIndex = 16;
      label8.Text = "Monospace:";
      // 
      // monospaceCheckBox
      // 
      monospaceCheckBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      monospaceCheckBox.AutoSize = true;
      monospaceCheckBox.Location = new Point(134, 62);
      monospaceCheckBox.Name = "monospaceCheckBox";
      monospaceCheckBox.Size = new Size(22, 21);
      monospaceCheckBox.TabIndex = 15;
      monospaceCheckBox.UseVisualStyleBackColor = true;
      monospaceCheckBox.CheckedChanged += monospaceCheckBox_CheckedChanged;
      // 
      // SaveButton
      // 
      SaveButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      SaveButton.Location = new Point(2356, 1177);
      SaveButton.Name = "SaveButton";
      SaveButton.Size = new Size(112, 34);
      SaveButton.TabIndex = 9;
      SaveButton.Text = "Download";
      SaveButton.UseVisualStyleBackColor = true;
      SaveButton.Click += SaveButton_Click;
      // 
      // label4
      // 
      label4.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label4.AutoSize = true;
      label4.Location = new Point(231, 27);
      label4.Name = "label4";
      label4.Size = new Size(31, 25);
      label4.TabIndex = 7;
      label4.Text = "px";
      // 
      // label7
      // 
      label7.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label7.AutoSize = true;
      label7.Location = new Point(56, 27);
      label7.Name = "label7";
      label7.Size = new Size(69, 25);
      label7.TabIndex = 6;
      label7.Text = "Height:";
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
      // label10
      // 
      label10.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      label10.AutoSize = true;
      label10.Location = new Point(2197, 1143);
      label10.Name = "label10";
      label10.Size = new Size(130, 25);
      label10.TabIndex = 21;
      label10.Text = "Font Sizes (px):";
      // 
      // fontSizesTextBox
      // 
      fontSizesTextBox.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      fontSizesTextBox.Location = new Point(2333, 1140);
      fontSizesTextBox.Name = "fontSizesTextBox";
      fontSizesTextBox.Size = new Size(135, 31);
      fontSizesTextBox.TabIndex = 22;
      fontSizesTextBox.Text = "8,16,24,32,40,48,56,64";
      // 
      // statusTextBox
      // 
      statusTextBox.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      statusTextBox.Location = new Point(2193, 861);
      statusTextBox.Multiline = true;
      statusTextBox.Name = "statusTextBox";
      statusTextBox.Size = new Size(275, 273);
      statusTextBox.TabIndex = 23;
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
      // testButton1
      // 
      testButton1.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      testButton1.Location = new Point(2193, 776);
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
      testButton2.Location = new Point(2286, 776);
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
      testButton3.Location = new Point(2381, 776);
      testButton3.Name = "testButton3";
      testButton3.Size = new Size(87, 79);
      testButton3.TabIndex = 25;
      testButton3.Text = "Font";
      testButton3.UseVisualStyleBackColor = true;
      testButton3.Click += testFontButton_Click;
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
      groupBox3.Location = new Point(2134, 46);
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
      // groupBox4
      // 
      groupBox4.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      groupBox4.Controls.Add(label14);
      groupBox4.Controls.Add(digitCheckBox);
      groupBox4.Controls.Add(label6);
      groupBox4.Controls.Add(label4);
      groupBox4.Controls.Add(label3);
      groupBox4.Controls.Add(SpacingUpDown);
      groupBox4.Controls.Add(label7);
      groupBox4.Controls.Add(charHeightUpDown);
      groupBox4.Controls.Add(label8);
      groupBox4.Controls.Add(monospaceCheckBox);
      groupBox4.Location = new Point(2134, 189);
      groupBox4.Name = "groupBox4";
      groupBox4.Size = new Size(334, 190);
      groupBox4.TabIndex = 27;
      groupBox4.TabStop = false;
      groupBox4.Text = "VLF Font";
      // 
      // label14
      // 
      label14.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label14.AutoSize = true;
      label14.Location = new Point(71, 87);
      label14.Name = "label14";
      label14.Size = new Size(54, 25);
      label14.TabIndex = 22;
      label14.Text = "Digit:";
      // 
      // digitCheckBox
      // 
      digitCheckBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      digitCheckBox.AutoSize = true;
      digitCheckBox.Location = new Point(134, 90);
      digitCheckBox.Name = "digitCheckBox";
      digitCheckBox.Size = new Size(22, 21);
      digitCheckBox.TabIndex = 21;
      digitCheckBox.UseVisualStyleBackColor = true;
      digitCheckBox.CheckedChanged += digitCheckBox_CheckedChanged;
      // 
      // label6
      // 
      label6.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label6.AutoSize = true;
      label6.Location = new Point(234, 119);
      label6.Name = "label6";
      label6.Size = new Size(31, 25);
      label6.TabIndex = 7;
      label6.Text = "px";
      // 
      // label3
      // 
      label3.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label3.AutoSize = true;
      label3.Location = new Point(46, 119);
      label3.Name = "label3";
      label3.Size = new Size(79, 25);
      label3.TabIndex = 6;
      label3.Text = "Spacing:";
      // 
      // SpacingUpDown
      // 
      SpacingUpDown.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      SpacingUpDown.Location = new Point(134, 117);
      SpacingUpDown.Name = "SpacingUpDown";
      SpacingUpDown.Size = new Size(94, 31);
      SpacingUpDown.TabIndex = 20;
      SpacingUpDown.Value = new decimal(new int[] { 1, 0, 0, 0 });
      SpacingUpDown.ValueChanged += SpacingUpDown_ValueChanged;
      // 
      // button1
      // 
      button1.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
      button1.Location = new Point(2197, 625);
      button1.Name = "button1";
      button1.Size = new Size(265, 46);
      button1.TabIndex = 25;
      button1.Text = "Load Roboto32.vlw";
      button1.UseVisualStyleBackColor = true;
      button1.Click += button1_Click;
      // 
      // MainForm
      // 
      AutoScaleDimensions = new SizeF(10F, 25F);
      AutoScaleMode = AutoScaleMode.Font;
      ClientSize = new Size(2480, 1223);
      Controls.Add(groupBox4);
      Controls.Add(groupBox3);
      Controls.Add(testButton3);
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
      groupBox6.ResumeLayout(false);
      groupBox6.PerformLayout();
      groupBox5.ResumeLayout(false);
      groupBox5.PerformLayout();
      ((System.ComponentModel.ISupportInitialize)magnificationUpDown).EndInit();
      tabPage2.ResumeLayout(false);
      ((System.ComponentModel.ISupportInitialize)charHeightUpDown).EndInit();
      groupBox3.ResumeLayout(false);
      groupBox3.PerformLayout();
      groupBox4.ResumeLayout(false);
      groupBox4.PerformLayout();
      ((System.ComponentModel.ISupportInitialize)SpacingUpDown).EndInit();
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
   private Panel GlyphCharPanel;
   private Label label8;
   private CheckBox monospaceCheckBox;
   private Label HexLabel;
   private Panel panel1;
   private NumericUpDown charHeightUpDown;
   private Label label10;
   private TextBox fontSizesTextBox;
   private TextBox statusTextBox;
   private CheckBox italicCheckBox;
   private Panel PreviewPanel;
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
   private Label label9;
   private NumericUpDown magnificationUpDown;
   private CheckBox showGlyphsCheckBox;
   private Label label15;
   private TextBox ascentTextBox;
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
   private TextBox heightTextBox;
   private TextBox widthTextBox;
   private TextBox descentTextBox;
   private GroupBox groupBox6;
   private GroupBox groupBox5;
   private Button button1;
   private Label label6;
   private Label label2;
   private Label label3;
   private Label label1;
   private NumericUpDown SpacingUpDown;
   private NumericUpDown numericUpDown1;
}

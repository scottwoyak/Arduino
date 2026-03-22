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
      PreviewPanel = new Panel();
      PreviewTextBox = new TextBox();
      HexLabel = new Label();
      GlyphCharPanel = new Panel();
      label5 = new Label();
      TestCharTextBox = new TextBox();
      TrueTypeCharPanel = new Panel();
      tabPage2 = new TabPage();
      panel1 = new Panel();
      tabPage3 = new TabPage();
      groupBox2 = new GroupBox();
      groupBox1 = new GroupBox();
      label9 = new Label();
      magnificationUpDown = new NumericUpDown();
      showGlyphsCheckBox = new CheckBox();
      charHeightUpDown = new NumericUpDown();
      label8 = new Label();
      MonospaceCheckBox = new CheckBox();
      DesiredWidthTextBox = new TextBox();
      ComputedWidthTextBox = new TextBox();
      label1 = new Label();
      SaveButton = new Button();
      label4 = new Label();
      label3 = new Label();
      label2 = new Label();
      label6 = new Label();
      label7 = new Label();
      FontComboBox = new ComboBox();
      label10 = new Label();
      fontSizesTextBox = new TextBox();
      statusTextBox = new TextBox();
      italicCheckBox = new CheckBox();
      testButton1 = new Button();
      testButton2 = new Button();
      testButton3 = new Button();
      tabControl1.SuspendLayout();
      tabPage1.SuspendLayout();
      tabPage2.SuspendLayout();
      tabPage3.SuspendLayout();
      groupBox1.SuspendLayout();
      ((System.ComponentModel.ISupportInitialize)magnificationUpDown).BeginInit();
      ((System.ComponentModel.ISupportInitialize)charHeightUpDown).BeginInit();
      SuspendLayout();
      // 
      // tabControl1
      // 
      tabControl1.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      tabControl1.Controls.Add(tabPage1);
      tabControl1.Controls.Add(tabPage2);
      tabControl1.Controls.Add(tabPage3);
      tabControl1.Location = new Point(12, 12);
      tabControl1.Name = "tabControl1";
      tabControl1.SelectedIndex = 0;
      tabControl1.Size = new Size(1545, 977);
      tabControl1.TabIndex = 0;
      // 
      // tabPage1
      // 
      tabPage1.Controls.Add(PreviewPanel);
      tabPage1.Controls.Add(PreviewTextBox);
      tabPage1.Controls.Add(HexLabel);
      tabPage1.Controls.Add(GlyphCharPanel);
      tabPage1.Controls.Add(label5);
      tabPage1.Controls.Add(TestCharTextBox);
      tabPage1.Controls.Add(TrueTypeCharPanel);
      tabPage1.Location = new Point(4, 34);
      tabPage1.Name = "tabPage1";
      tabPage1.Padding = new Padding(6);
      tabPage1.Size = new Size(1537, 939);
      tabPage1.TabIndex = 0;
      tabPage1.Text = "True Type";
      tabPage1.UseVisualStyleBackColor = true;
      // 
      // PreviewPanel
      // 
      PreviewPanel.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      PreviewPanel.BackColor = Color.Black;
      PreviewPanel.Location = new Point(616, 9);
      PreviewPanel.Name = "PreviewPanel";
      PreviewPanel.Size = new Size(912, 208);
      PreviewPanel.TabIndex = 20;
      PreviewPanel.Paint += PreviewPanel_Paint;
      // 
      // PreviewTextBox
      // 
      PreviewTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      PreviewTextBox.BackColor = Color.Black;
      PreviewTextBox.Font = new Font("Segoe UI", 12F, FontStyle.Regular, GraphicsUnit.Point, 0);
      PreviewTextBox.ForeColor = SystemColors.Window;
      PreviewTextBox.Location = new Point(9, 9);
      PreviewTextBox.Multiline = true;
      PreviewTextBox.Name = "PreviewTextBox";
      PreviewTextBox.Size = new Size(601, 208);
      PreviewTextBox.TabIndex = 19;
      PreviewTextBox.Text = "Enter text to display here\r\n\r\n0123456789\r\nABCDEFGHIJKLMNOPQRSTUVWXYZ\r\nabcdefghijklmnopqrstuvwxyz";
      // 
      // HexLabel
      // 
      HexLabel.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      HexLabel.AutoSize = true;
      HexLabel.Location = new Point(176, 902);
      HexLabel.Name = "HexLabel";
      HexLabel.Size = new Size(50, 25);
      HexLabel.TabIndex = 18;
      HexLabel.Text = "0x00";
      // 
      // GlyphCharPanel
      // 
      GlyphCharPanel.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
      GlyphCharPanel.BackColor = Color.DimGray;
      GlyphCharPanel.Location = new Point(616, 223);
      GlyphCharPanel.Name = "GlyphCharPanel";
      GlyphCharPanel.Size = new Size(912, 670);
      GlyphCharPanel.TabIndex = 2;
      // 
      // label5
      // 
      label5.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      label5.AutoSize = true;
      label5.Location = new Point(9, 902);
      label5.Name = "label5";
      label5.Size = new Size(117, 25);
      label5.TabIndex = 6;
      label5.Text = "Preview Char:";
      // 
      // TestCharTextBox
      // 
      TestCharTextBox.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      TestCharTextBox.Location = new Point(132, 899);
      TestCharTextBox.Name = "TestCharTextBox";
      TestCharTextBox.Size = new Size(38, 31);
      TestCharTextBox.TabIndex = 5;
      TestCharTextBox.Text = "$";
      TestCharTextBox.TextChanged += TestCharTextBox_TextChanged;
      // 
      // TrueTypeCharPanel
      // 
      TrueTypeCharPanel.BackColor = Color.DimGray;
      TrueTypeCharPanel.Location = new Point(9, 223);
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
      tabPage2.Size = new Size(1537, 939);
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
      panel1.Size = new Size(1519, 921);
      panel1.TabIndex = 0;
      panel1.Paint += panel1_Paint;
      // 
      // tabPage3
      // 
      tabPage3.Controls.Add(groupBox2);
      tabPage3.Controls.Add(groupBox1);
      tabPage3.Location = new Point(4, 34);
      tabPage3.Name = "tabPage3";
      tabPage3.Padding = new Padding(6);
      tabPage3.Size = new Size(1537, 939);
      tabPage3.TabIndex = 2;
      tabPage3.Text = "VLF";
      tabPage3.UseVisualStyleBackColor = true;
      // 
      // groupBox2
      // 
      groupBox2.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
      groupBox2.Location = new Point(9, 9);
      groupBox2.Name = "groupBox2";
      groupBox2.Padding = new Padding(6);
      groupBox2.Size = new Size(1519, 363);
      groupBox2.TabIndex = 1;
      groupBox2.TabStop = false;
      groupBox2.Text = "Text";
      // 
      // groupBox1
      // 
      groupBox1.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      groupBox1.Controls.Add(label9);
      groupBox1.Controls.Add(magnificationUpDown);
      groupBox1.Controls.Add(showGlyphsCheckBox);
      groupBox1.Location = new Point(9, 378);
      groupBox1.Name = "groupBox1";
      groupBox1.Padding = new Padding(6);
      groupBox1.Size = new Size(1519, 552);
      groupBox1.TabIndex = 0;
      groupBox1.TabStop = false;
      groupBox1.Text = "Preview";
      // 
      // label9
      // 
      label9.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      label9.AutoSize = true;
      label9.Location = new Point(218, 515);
      label9.Name = "label9";
      label9.Size = new Size(120, 25);
      label9.TabIndex = 3;
      label9.Text = "Magnification";
      // 
      // magnificationUpDown
      // 
      magnificationUpDown.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      magnificationUpDown.Location = new Point(344, 513);
      magnificationUpDown.Minimum = new decimal(new int[] { 1, 0, 0, 0 });
      magnificationUpDown.Name = "magnificationUpDown";
      magnificationUpDown.Size = new Size(68, 31);
      magnificationUpDown.TabIndex = 2;
      magnificationUpDown.Value = new decimal(new int[] { 1, 0, 0, 0 });
      magnificationUpDown.ValueChanged += magnificationUpDown_ValueChanged;
      // 
      // showGlyphsCheckBox
      // 
      showGlyphsCheckBox.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
      showGlyphsCheckBox.AutoSize = true;
      showGlyphsCheckBox.Location = new Point(9, 514);
      showGlyphsCheckBox.Name = "showGlyphsCheckBox";
      showGlyphsCheckBox.Size = new Size(141, 29);
      showGlyphsCheckBox.TabIndex = 1;
      showGlyphsCheckBox.Text = "Show Glyphs";
      showGlyphsCheckBox.UseVisualStyleBackColor = true;
      showGlyphsCheckBox.CheckedChanged += showGlyphsCheckBox_CheckedChanged;
      // 
      // charHeightUpDown
      // 
      charHeightUpDown.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      charHeightUpDown.Location = new Point(1703, 94);
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
      label8.Location = new Point(1588, 216);
      label8.Name = "label8";
      label8.Size = new Size(109, 25);
      label8.TabIndex = 16;
      label8.Text = "Monospace:";
      // 
      // MonospaceCheckBox
      // 
      MonospaceCheckBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      MonospaceCheckBox.AutoSize = true;
      MonospaceCheckBox.Location = new Point(1712, 220);
      MonospaceCheckBox.Name = "MonospaceCheckBox";
      MonospaceCheckBox.Size = new Size(22, 21);
      MonospaceCheckBox.TabIndex = 15;
      MonospaceCheckBox.UseVisualStyleBackColor = true;
      // 
      // DesiredWidthTextBox
      // 
      DesiredWidthTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      DesiredWidthTextBox.Location = new Point(1712, 292);
      DesiredWidthTextBox.Name = "DesiredWidthTextBox";
      DesiredWidthTextBox.Size = new Size(94, 31);
      DesiredWidthTextBox.TabIndex = 14;
      DesiredWidthTextBox.Text = "24";
      // 
      // ComputedWidthTextBox
      // 
      ComputedWidthTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      ComputedWidthTextBox.Location = new Point(1712, 253);
      ComputedWidthTextBox.Name = "ComputedWidthTextBox";
      ComputedWidthTextBox.ReadOnly = true;
      ComputedWidthTextBox.Size = new Size(94, 31);
      ComputedWidthTextBox.TabIndex = 13;
      ComputedWidthTextBox.Text = "32";
      // 
      // label1
      // 
      label1.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label1.AutoSize = true;
      label1.Location = new Point(1642, 256);
      label1.Name = "label1";
      label1.Size = new Size(64, 25);
      label1.TabIndex = 10;
      label1.Text = "Width:";
      // 
      // SaveButton
      // 
      SaveButton.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      SaveButton.Location = new Point(1726, 955);
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
      label4.Location = new Point(1803, 96);
      label4.Name = "label4";
      label4.Size = new Size(31, 25);
      label4.TabIndex = 7;
      label4.Text = "px";
      // 
      // label3
      // 
      label3.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label3.AutoSize = true;
      label3.Location = new Point(1812, 256);
      label3.Name = "label3";
      label3.Size = new Size(31, 25);
      label3.TabIndex = 7;
      label3.Text = "px";
      // 
      // label2
      // 
      label2.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label2.AutoSize = true;
      label2.Location = new Point(1812, 295);
      label2.Name = "label2";
      label2.Size = new Size(31, 25);
      label2.TabIndex = 7;
      label2.Text = "px";
      // 
      // label6
      // 
      label6.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label6.AutoSize = true;
      label6.Location = new Point(1577, 295);
      label6.Name = "label6";
      label6.Size = new Size(129, 25);
      label6.TabIndex = 7;
      label6.Text = "Desired Width:";
      // 
      // label7
      // 
      label7.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label7.AutoSize = true;
      label7.Location = new Point(1563, 96);
      label7.Name = "label7";
      label7.Size = new Size(134, 25);
      label7.TabIndex = 6;
      label7.Text = "Desired Height:";
      // 
      // FontComboBox
      // 
      FontComboBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      FontComboBox.FormattingEnabled = true;
      FontComboBox.Location = new Point(1563, 55);
      FontComboBox.Name = "FontComboBox";
      FontComboBox.Size = new Size(275, 33);
      FontComboBox.TabIndex = 3;
      // 
      // label10
      // 
      label10.AutoSize = true;
      label10.Location = new Point(1567, 921);
      label10.Name = "label10";
      label10.Size = new Size(130, 25);
      label10.TabIndex = 21;
      label10.Text = "Font Sizes (px):";
      // 
      // fontSizesTextBox
      // 
      fontSizesTextBox.Location = new Point(1703, 918);
      fontSizesTextBox.Name = "fontSizesTextBox";
      fontSizesTextBox.Size = new Size(135, 31);
      fontSizesTextBox.TabIndex = 22;
      fontSizesTextBox.Text = "8,16,24,32,40,48,56,64";
      // 
      // statusTextBox
      // 
      statusTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      statusTextBox.Location = new Point(1563, 639);
      statusTextBox.Multiline = true;
      statusTextBox.Name = "statusTextBox";
      statusTextBox.Size = new Size(275, 273);
      statusTextBox.TabIndex = 23;
      // 
      // italicCheckBox
      // 
      italicCheckBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      italicCheckBox.AutoSize = true;
      italicCheckBox.Location = new Point(1601, 158);
      italicCheckBox.Name = "italicCheckBox";
      italicCheckBox.Size = new Size(74, 29);
      italicCheckBox.TabIndex = 24;
      italicCheckBox.Text = "Italic";
      italicCheckBox.UseVisualStyleBackColor = true;
      italicCheckBox.CheckedChanged += italicCheckBox_CheckedChanged;
      // 
      // testButton1
      // 
      testButton1.Location = new Point(1563, 554);
      testButton1.Name = "testButton1";
      testButton1.Size = new Size(87, 79);
      testButton1.TabIndex = 25;
      testButton1.Text = "Observe";
      testButton1.UseVisualStyleBackColor = true;
      testButton1.Click += testObserveButton_Click;
      // 
      // testButton2
      // 
      testButton2.Location = new Point(1656, 554);
      testButton2.Name = "testButton2";
      testButton2.Size = new Size(89, 79);
      testButton2.TabIndex = 25;
      testButton2.Text = "Glyph";
      testButton2.UseVisualStyleBackColor = true;
      testButton2.Click += testGlyphButton_Click;
      // 
      // testButton3
      // 
      testButton3.Location = new Point(1751, 554);
      testButton3.Name = "testButton3";
      testButton3.Size = new Size(87, 79);
      testButton3.TabIndex = 25;
      testButton3.Text = "Font";
      testButton3.UseVisualStyleBackColor = true;
      testButton3.Click += testFontButton_Click;
      // 
      // MainForm
      // 
      AutoScaleDimensions = new SizeF(10F, 25F);
      AutoScaleMode = AutoScaleMode.Font;
      ClientSize = new Size(1850, 1001);
      Controls.Add(testButton3);
      Controls.Add(testButton2);
      Controls.Add(testButton1);
      Controls.Add(italicCheckBox);
      Controls.Add(statusTextBox);
      Controls.Add(fontSizesTextBox);
      Controls.Add(label10);
      Controls.Add(charHeightUpDown);
      Controls.Add(tabControl1);
      Controls.Add(FontComboBox);
      Controls.Add(label8);
      Controls.Add(SaveButton);
      Controls.Add(MonospaceCheckBox);
      Controls.Add(label7);
      Controls.Add(label4);
      Controls.Add(DesiredWidthTextBox);
      Controls.Add(label6);
      Controls.Add(ComputedWidthTextBox);
      Controls.Add(label2);
      Controls.Add(label1);
      Controls.Add(label3);
      Name = "MainForm";
      Text = "Smooth Font Creator for TFT_eSPI";
      tabControl1.ResumeLayout(false);
      tabPage1.ResumeLayout(false);
      tabPage1.PerformLayout();
      tabPage2.ResumeLayout(false);
      tabPage3.ResumeLayout(false);
      groupBox1.ResumeLayout(false);
      groupBox1.PerformLayout();
      ((System.ComponentModel.ISupportInitialize)magnificationUpDown).EndInit();
      ((System.ComponentModel.ISupportInitialize)charHeightUpDown).EndInit();
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
   private Label label6;
   private Label label7;
   private Button SaveButton;
   private Label label1;
   private TextBox DesiredWidthTextBox;
   private TextBox ComputedWidthTextBox;
   private Label label4;
   private Label label3;
   private Label label2;
   private Label label5;
   private Panel GlyphCharPanel;
   private Label label8;
   private CheckBox MonospaceCheckBox;
   private Label HexLabel;
   private Panel panel1;
   private TabPage tabPage3;
   private GroupBox groupBox2;
   private GroupBox groupBox1;
   private CheckBox showGlyphsCheckBox;
   private Label label9;
   private NumericUpDown magnificationUpDown;
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
}
